#include "../../include/hubsight/admin/config/hscfg_importer.h"

#include <QCryptographicHash>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <algorithm>

#if defined(HUBSIGHT_ADMIN_HSCFG_AVAILABLE)
#include <argon2.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <yaml.h>
#include <zip.h>
#endif

namespace HubSight::Admin {
namespace {

constexpr char kAdminMagic[] = "HSCFG\x02";
constexpr qsizetype kMagicSize = 6;
constexpr qsizetype kSaltSize = 32;
constexpr qsizetype kNonceSize = 12;
constexpr qsizetype kGcmTagSize = 16;
constexpr qsizetype kDerivedKeySize = 32;
constexpr qsizetype kMaxContainerSize = 64 * 1024 * 1024;
constexpr qsizetype kMaxZipEntrySize = 16 * 1024 * 1024;
constexpr qsizetype kMaxPayloadSize = 32 * 1024 * 1024;

HscfgImportResult failure(HscfgImportError error, const QString &message) {
  HscfgImportResult result;
  result.error = error;
  result.message = message;
  return result;
}

bool isLoopback(const QString &host) {
  return host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0 ||
         host == QStringLiteral("127.0.0.1") || host == QStringLiteral("::1");
}

bool validPin(const QString &pin) {
  return pin.size() == 6 &&
         std::all_of(pin.cbegin(), pin.cend(), [](QChar character) {
           return character >= QLatin1Char('0') &&
                  character <= QLatin1Char('9');
         });
}

QString normalizedPath(QString path) {
  while (path.size() > 1 && path.endsWith(QLatin1Char('/'))) {
    path.chop(1);
  }
  if (path.isEmpty()) {
    path = QStringLiteral("/");
  }
  return path;
}

bool validUrl(const QUrl &url, bool websocket, const QString &label,
              QString *reason) {
  const QString scheme = url.scheme().toLower();
  const bool secure = websocket ? scheme == QStringLiteral("wss")
                                : scheme == QStringLiteral("https");
  const bool insecureLoopback =
      isLoopback(url.host()) && (websocket ? scheme == QStringLiteral("ws")
                                           : scheme == QStringLiteral("http"));
  if (!url.isValid() || url.host().isEmpty() ||
      (!secure && !insecureLoopback) || !url.userInfo().isEmpty() ||
      !url.query().isEmpty() || !url.fragment().isEmpty()) {
    if (reason) {
      *reason =
          label +
          QStringLiteral(" must use HTTPS/WSS (loopback HTTP/WS is allowed "
                         "only for tests) and must not contain credentials.");
    }
    return false;
  }
  return true;
}

int effectivePort(const QUrl &url) {
  if (url.port(-1) >= 0) {
    return url.port();
  }
  const QString scheme = url.scheme().toLower();
  return (scheme == QStringLiteral("https") || scheme == QStringLiteral("wss"))
             ? 443
             : (scheme == QStringLiteral("http") ||
                        scheme == QStringLiteral("ws")
                    ? 80
                    : -1);
}

bool sameOrigin(const QUrl &left, const QUrl &right) {
  return left.host().compare(right.host(), Qt::CaseInsensitive) == 0 &&
         effectivePort(left) == effectivePort(right);
}

#if defined(HUBSIGHT_ADMIN_HSCFG_AVAILABLE)

using StringMap = QHash<QString, QString>;

bool parseYamlScalars(const QByteArray &yamlBytes, StringMap *values,
                      QString *reason) {
  yaml_parser_t parser;
  if (!yaml_parser_initialize(&parser)) {
    if (reason) {
      *reason = QStringLiteral("Unable to initialize the YAML parser.");
    }
    return false;
  }

  yaml_parser_set_input_string(
      &parser, reinterpret_cast<const unsigned char *>(yamlBytes.constData()),
      static_cast<size_t>(yamlBytes.size()));

  yaml_event_t event;
  bool streamStarted = false;
  bool documentStarted = false;
  bool mappingStarted = false;
  bool expectingKey = true;
  QString currentKey;
  bool ok = true;
  QString localReason;

  while (ok && yaml_parser_parse(&parser, &event)) {
    switch (event.type) {
    case YAML_STREAM_START_EVENT:
      streamStarted = true;
      break;
    case YAML_DOCUMENT_START_EVENT:
      if (!streamStarted || documentStarted) {
        ok = false;
        localReason = QStringLiteral("YAML document structure is invalid.");
      }
      documentStarted = true;
      break;
    case YAML_MAPPING_START_EVENT:
      if (!documentStarted || mappingStarted) {
        ok = false;
        localReason =
            QStringLiteral("Only one top-level YAML mapping is accepted.");
      }
      mappingStarted = true;
      break;
    case YAML_SCALAR_EVENT: {
      const QString scalar = QString::fromUtf8(
          reinterpret_cast<const char *>(event.data.scalar.value),
          static_cast<int>(event.data.scalar.length));
      if (!mappingStarted) {
        ok = false;
        localReason = QStringLiteral("YAML root must be a mapping.");
      } else if (expectingKey) {
        currentKey = scalar;
        expectingKey = false;
      } else {
        values->insert(currentKey, scalar);
        currentKey.clear();
        expectingKey = true;
      }
      break;
    }
    case YAML_MAPPING_END_EVENT:
      if (!mappingStarted || !expectingKey) {
        ok = false;
        localReason =
            QStringLiteral("YAML mapping contains an incomplete entry.");
      }
      break;
    case YAML_DOCUMENT_END_EVENT:
      if (!mappingStarted || !expectingKey) {
        ok = false;
        localReason = QStringLiteral("YAML document is incomplete.");
      }
      break;
    case YAML_STREAM_END_EVENT:
      if (!documentStarted || !mappingStarted || !expectingKey) {
        ok = false;
        localReason = QStringLiteral("YAML stream is incomplete.");
      }
      break;
    default:
      ok = false;
      localReason = QStringLiteral(
          "Nested YAML values and aliases are not allowed in .hscfg metadata.");
      break;
    }

    const yaml_event_type_t eventType = event.type;
    yaml_event_delete(&event);
    if (eventType == YAML_STREAM_END_EVENT) {
      break;
    }
  }

  if (ok && parser.error != YAML_NO_ERROR) {
    ok = false;
    localReason = QStringLiteral("YAML parse error.");
  }
  yaml_parser_delete(&parser);

  if (!ok && reason) {
    *reason =
        localReason.isEmpty() ? QStringLiteral("Invalid YAML.") : localReason;
  }
  return ok;
}

bool readZipPayload(const QByteArray &zipBytes,
                    QHash<QString, QByteArray> *files, QString *reason) {
  if (zipBytes.isEmpty() || zipBytes.size() > kMaxPayloadSize) {
    if (reason) {
      *reason = QStringLiteral("The decrypted .hscfg payload is too large.");
    }
    return false;
  }

  zip_error_t error;
  zip_error_init(&error);
  zip_source_t *source = zip_source_buffer_create(
      zipBytes.constData(), static_cast<zip_uint64_t>(zipBytes.size()), 0,
      &error);
  if (!source) {
    if (reason) {
      *reason = QStringLiteral("Unable to open the in-memory ZIP payload.");
    }
    zip_error_fini(&error);
    return false;
  }

  zip_t *archive = zip_open_from_source(source, ZIP_RDONLY, &error);
  if (!archive) {
    zip_source_free(source);
    if (reason) {
      *reason =
          QStringLiteral("The decrypted .hscfg payload is not a valid ZIP.");
    }
    zip_error_fini(&error);
    return false;
  }

  const QSet<QString> allowed = {
      QStringLiteral("metadata.yml"), QStringLiteral("urls.yml"),
      QStringLiteral("key.yml"), QStringLiteral("ca_cert.pem")};
  bool ok = true;
  QString localReason;
  const zip_int64_t entryCount = zip_get_num_entries(archive, 0);
  for (zip_uint64_t index = 0;
       ok && index < static_cast<zip_uint64_t>(entryCount); ++index) {
    const char *entryName = zip_get_name(archive, index, ZIP_FL_ENC_GUESS);
    const QString name = entryName ? QString::fromUtf8(entryName) : QString();
    if (!allowed.contains(name) || files->contains(name) ||
        name.endsWith('/')) {
      ok = false;
      localReason =
          QStringLiteral("The .hscfg ZIP contains an unexpected entry.");
      break;
    }

    zip_stat_t stat;
    zip_stat_init(&stat);
    if (zip_stat_index(archive, index, 0, &stat) != 0 ||
        stat.size > static_cast<zip_uint64_t>(kMaxZipEntrySize)) {
      ok = false;
      localReason =
          QStringLiteral("The .hscfg ZIP contains an oversized entry.");
      break;
    }

    zip_file_t *file = zip_fopen_index(archive, index, 0);
    if (!file) {
      ok = false;
      localReason = QStringLiteral("Unable to read a .hscfg ZIP entry.");
      break;
    }
    QByteArray content(static_cast<qsizetype>(stat.size), Qt::Uninitialized);
    const zip_int64_t read = zip_fread(file, content.data(), stat.size);
    zip_fclose(file);
    if (read != static_cast<zip_int64_t>(stat.size)) {
      ok = false;
      localReason = QStringLiteral("A .hscfg ZIP entry is truncated.");
      break;
    }
    files->insert(name, content);
  }

  if (zip_close(archive) != 0) {
    ok = false;
    localReason = QStringLiteral("Unable to close the .hscfg ZIP payload.");
  }
  zip_error_fini(&error);

  if (!ok && reason) {
    *reason = localReason;
  }
  return ok;
}

QByteArray decodeBinary(QString value) {
  value = value.trimmed();
  if (value.isEmpty()) {
    return {};
  }
  const QRegularExpression hexPattern(QStringLiteral("^[0-9a-fA-F]+$"));
  if ((value.size() % 2) == 0 && hexPattern.match(value).hasMatch()) {
    return QByteArray::fromHex(value.toLatin1());
  }
  return QByteArray::fromBase64(value.toLatin1());
}

QByteArray parsePublicKey(const QByteArray &value) {
  if (value.size() == 32) {
    return value;
  }
  if (value.startsWith("-----BEGIN")) {
    BIO *bio = BIO_new_mem_buf(value.constData(), value.size());
    if (!bio) {
      return {};
    }
    EVP_PKEY *key = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!key) {
      return {};
    }
    QByteArray raw(32, Qt::Uninitialized);
    size_t length = static_cast<size_t>(raw.size());
    const bool success =
        EVP_PKEY_get_raw_public_key(
            key, reinterpret_cast<unsigned char *>(raw.data()), &length) == 1 &&
        length == 32;
    EVP_PKEY_free(key);
    return success ? raw : QByteArray();
  }
  const QByteArray decoded = decodeBinary(QString::fromUtf8(value));
  return decoded.size() == 32 ? decoded : QByteArray();
}

bool verifyEd25519(const QByteArray &data, const QByteArray &signature,
                   const QByteArray &publicKey) {
  if (signature.size() != 64 || publicKey.size() != 32) {
    return false;
  }
  EVP_PKEY *key = EVP_PKEY_new_raw_public_key(
      EVP_PKEY_ED25519, nullptr,
      reinterpret_cast<const unsigned char *>(publicKey.constData()),
      static_cast<size_t>(publicKey.size()));
  if (!key) {
    return false;
  }
  EVP_MD_CTX *context = EVP_MD_CTX_new();
  const bool valid =
      context &&
      EVP_DigestVerifyInit(context, nullptr, nullptr, nullptr, key) == 1 &&
      EVP_DigestVerify(
          context,
          reinterpret_cast<const unsigned char *>(signature.constData()),
          static_cast<size_t>(signature.size()),
          reinterpret_cast<const unsigned char *>(data.constData()),
          static_cast<size_t>(data.size())) == 1;
  EVP_MD_CTX_free(context);
  EVP_PKEY_free(key);
  return valid;
}

bool decryptContainer(const QByteArray &container, const QString &pin,
                      QByteArray *zipBytes, HscfgImportResult *failureResult) {
  if (container.size() < kMagicSize + kSaltSize + kNonceSize + kGcmTagSize) {
    *failureResult =
        failure(HscfgImportError::InvalidContainer,
                QStringLiteral("The .hscfg container is too short."));
    return false;
  }
  if (container.left(kMagicSize) != QByteArray(kAdminMagic, kMagicSize)) {
    *failureResult = failure(
        HscfgImportError::InvalidMagic,
        QStringLiteral("Only Admin .hscfg v2 containers are accepted."));
    return false;
  }

  const QByteArray salt = container.mid(kMagicSize, kSaltSize);
  const QByteArray nonce = container.mid(kMagicSize + kSaltSize, kNonceSize);
  const QByteArray encrypted =
      container.mid(kMagicSize + kSaltSize + kNonceSize);
  const QByteArray ciphertext = encrypted.left(encrypted.size() - kGcmTagSize);
  const QByteArray tag = encrypted.right(kGcmTagSize);

  QByteArray key(kDerivedKeySize, Qt::Uninitialized);
  const QByteArray pinBytes = pin.toUtf8();
  const int argonResult = argon2id_hash_raw(
      4, 64 * 1024, 2, pinBytes.constData(),
      static_cast<size_t>(pinBytes.size()),
      reinterpret_cast<const unsigned char *>(salt.constData()),
      static_cast<size_t>(salt.size()),
      reinterpret_cast<unsigned char *>(key.data()),
      static_cast<size_t>(key.size()));
  if (argonResult != ARGON2_OK) {
    OPENSSL_cleanse(key.data(), static_cast<size_t>(key.size()));
    *failureResult =
        failure(HscfgImportError::DecryptionFailed,
                QStringLiteral("Unable to derive the .hscfg decryption key."));
    return false;
  }

  EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
  bool ok =
      context &&
      EVP_DecryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr,
                         nullptr) == 1 &&
      EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN,
                          static_cast<int>(nonce.size()), nullptr) == 1 &&
      EVP_DecryptInit_ex(
          context, nullptr, nullptr,
          reinterpret_cast<const unsigned char *>(key.constData()),
          reinterpret_cast<const unsigned char *>(nonce.constData())) == 1;

  int written = 0;
  if (ok) {
    ok = EVP_DecryptUpdate(context, nullptr, &written,
                           reinterpret_cast<const unsigned char *>(kAdminMagic),
                           kMagicSize) == 1;
  }
  QByteArray plaintext(ciphertext.size(), Qt::Uninitialized);
  if (ok && !ciphertext.isEmpty()) {
    ok = EVP_DecryptUpdate(
             context, reinterpret_cast<unsigned char *>(plaintext.data()),
             &written,
             reinterpret_cast<const unsigned char *>(ciphertext.constData()),
             ciphertext.size()) == 1;
  }
  int finalWritten = 0;
  if (ok) {
    ok = EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_TAG,
                             static_cast<int>(tag.size()),
                             const_cast<char *>(tag.constData())) == 1 &&
         EVP_DecryptFinal_ex(
             context,
             reinterpret_cast<unsigned char *>(plaintext.data()) + written,
             &finalWritten) == 1;
  }
  EVP_CIPHER_CTX_free(context);
  OPENSSL_cleanse(key.data(), static_cast<size_t>(key.size()));

  if (!ok) {
    plaintext.fill('\0');
    *failureResult =
        failure(HscfgImportError::DecryptionFailed,
                QStringLiteral("Unable to decrypt the .hscfg container; the "
                               "PIN or data may be invalid."));
    return false;
  }
  plaintext.resize(written + finalWritten);
  *zipBytes = plaintext;
  return true;
}

#endif // HUBSIGHT_ADMIN_HSCFG_AVAILABLE

} // namespace

void HscfgImporter::setTrustedEd25519PublicKey(const QByteArray &publicKey) {
  m_trustedEd25519PublicKey = publicKey;
}

QByteArray HscfgImporter::trustedEd25519PublicKey() const {
  return m_trustedEd25519PublicKey;
}

HscfgImportResult HscfgImporter::importAdmin(const QByteArray &container,
                                             const QString &pin) const {
  if (container.size() > kMaxContainerSize) {
    return failure(HscfgImportError::InvalidContainer,
                   QStringLiteral("The .hscfg container is too large."));
  }
  if (!validPin(pin)) {
    return failure(
        HscfgImportError::InvalidPin,
        QStringLiteral("The .hscfg PIN must contain exactly six digits."));
  }
  if (container.size() < kMagicSize ||
      container.left(kMagicSize) != QByteArray(kAdminMagic, kMagicSize)) {
    return failure(
        HscfgImportError::InvalidMagic,
        QStringLiteral("Only Admin .hscfg v2 containers are accepted."));
  }
#if !defined(HUBSIGHT_ADMIN_HSCFG_AVAILABLE)
  return failure(
      HscfgImportError::ImporterUnavailable,
      QStringLiteral(
          "The .hscfg importer is unavailable in this build. Enable it and "
          "install OpenSSL, libargon2, libzip, and libyaml."));
#else
  QByteArray zipBytes;
  HscfgImportResult decryptionFailure;
  if (!decryptContainer(container, pin, &zipBytes, &decryptionFailure)) {
    return decryptionFailure;
  }

  QHash<QString, QByteArray> files;
  QString reason;
  if (!readZipPayload(zipBytes, &files, &reason)) {
    return failure(HscfgImportError::ZipInvalid, reason);
  }
  for (const QString &required :
       {QStringLiteral("metadata.yml"), QStringLiteral("urls.yml"),
        QStringLiteral("key.yml")}) {
    if (!files.contains(required)) {
      return failure(
          HscfgImportError::UnexpectedPayload,
          QStringLiteral("The Admin .hscfg ZIP is missing a required entry."));
    }
  }

  StringMap metadataValues;
  StringMap urlValues;
  StringMap keyValues;
  if (!parseYamlScalars(files.value(QStringLiteral("metadata.yml")),
                        &metadataValues, &reason) ||
      !parseYamlScalars(files.value(QStringLiteral("urls.yml")), &urlValues,
                        &reason) ||
      !parseYamlScalars(files.value(QStringLiteral("key.yml")), &keyValues,
                        &reason)) {
    return failure(HscfgImportError::YamlInvalid, reason);
  }

  HscfgConfig config;
  config.metadata.version = metadataValues.value(QStringLiteral("version"));
  config.metadata.profile = metadataValues.value(QStringLiteral("profile"));
  config.metadata.apiNamespace =
      metadataValues.value(QStringLiteral("api_namespace"));
  config.metadata.realtimeNamespace =
      metadataValues.value(QStringLiteral("realtime_namespace"));
  config.metadata.fcmEnabled =
      metadataValues.value(QStringLiteral("fcm_enabled"))
          .compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
  config.metadata.contentSha256 =
      metadataValues.value(QStringLiteral("content_sha256")).toLower();
  config.metadata.signatureAlgorithm =
      metadataValues.value(QStringLiteral("signature_algorithm"));
  config.metadata.digitalSignature =
      decodeBinary(metadataValues.value(QStringLiteral("digital_signature")));
  config.metadata.publicKey = parsePublicKey(
      metadataValues.value(QStringLiteral("ed25519_public_key")).toUtf8());
  config.urls.gatewayUrl = QUrl(urlValues.value(QStringLiteral("gateway_url")));
  config.urls.apiBaseUrl =
      QUrl(urlValues.value(QStringLiteral("api_base_url")));
  config.urls.relayWebSocketUrl =
      QUrl(urlValues.value(QStringLiteral("relay_ws_url")));
  config.urls.webRtcBaseUrl =
      QUrl(urlValues.value(QStringLiteral("webrtc_base_url")));
  config.urls.webRtcSignalingUrl =
      QUrl(urlValues.value(QStringLiteral("webrtc_signaling_url")));
  bool portOk = false;
  const QString portValue =
      urlValues.value(QStringLiteral("webrtc_media_port"));
  config.urls.webRtcMediaPort =
      portValue.isEmpty() ? 8555 : portValue.toInt(&portOk);
  if (!portValue.isEmpty() && !portOk) {
    return failure(HscfgImportError::ValidationFailed,
                   QStringLiteral("The WebRTC media port is invalid."));
  }

  config.identity.clientId = keyValues.value(QStringLiteral("client_id"));
  config.identity.clientName = keyValues.value(QStringLiteral("client_name"));
  config.identity.apiKey = keyValues.value(QStringLiteral("api_key"));
  config.identity.platform = keyValues.value(QStringLiteral("platform"));
  config.identity.audience = keyValues.value(QStringLiteral("audience"));
  config.identity.authMode = keyValues.value(QStringLiteral("auth_mode"));
  config.caCertificatePem = files.value(QStringLiteral("ca_cert.pem"));

  if (config.metadata.version != QStringLiteral("2.0") ||
      config.metadata.profile != QStringLiteral("admin_api") ||
      config.metadata.apiNamespace != QStringLiteral("/api/admin/v1") ||
      config.metadata.realtimeNamespace != QStringLiteral("/relay/admin/v1") ||
      config.metadata.fcmEnabled ||
      config.identity.platform != QStringLiteral("admin_desktop") ||
      config.identity.audience != QStringLiteral("admin_api") ||
      config.identity.authMode != QStringLiteral("bearer_jwt_plus_api_key")) {
    return failure(
        HscfgImportError::ValidationFailed,
        QStringLiteral(
            "The .hscfg profile is not a valid Admin API v2 profile."));
  }

  if (!validUrl(config.urls.gatewayUrl, false, QStringLiteral("gateway_url"),
                &reason) ||
      !validUrl(config.urls.apiBaseUrl, false, QStringLiteral("api_base_url"),
                &reason) ||
      !validUrl(config.urls.relayWebSocketUrl, true,
                QStringLiteral("relay_ws_url"), &reason) ||
      !validUrl(config.urls.webRtcBaseUrl, false,
                QStringLiteral("webrtc_base_url"), &reason) ||
      !validUrl(config.urls.webRtcSignalingUrl, false,
                QStringLiteral("webrtc_signaling_url"), &reason)) {
    return failure(HscfgImportError::ValidationFailed, reason);
  }
  if (!sameOrigin(config.urls.gatewayUrl, config.urls.apiBaseUrl) ||
      !sameOrigin(config.urls.gatewayUrl, config.urls.relayWebSocketUrl) ||
      !sameOrigin(config.urls.gatewayUrl, config.urls.webRtcSignalingUrl)) {
    return failure(HscfgImportError::ValidationFailed,
                   QStringLiteral("Admin REST and WebRTC signaling URLs must "
                                  "use the gateway origin."));
  }
  if (!normalizedPath(config.urls.apiBaseUrl.path())
           .endsWith(QStringLiteral("/api")) ||
      !normalizedPath(config.urls.relayWebSocketUrl.path())
           .startsWith(QStringLiteral("/relay")) ||
      !normalizedPath(config.urls.webRtcSignalingUrl.path())
           .startsWith(QStringLiteral("/webrtc"))) {
    return failure(
        HscfgImportError::ValidationFailed,
        QStringLiteral(
            "The Admin .hscfg URL paths do not match the gateway contract."));
  }
  if (config.urls.webRtcMediaPort < 1 || config.urls.webRtcMediaPort > 65535) {
    return failure(HscfgImportError::ValidationFailed,
                   QStringLiteral("The WebRTC media port is invalid."));
  }
  if (config.identity.clientId.trimmed().isEmpty() ||
      config.identity.apiKey.trimmed().isEmpty()) {
    return failure(
        HscfgImportError::ValidationFailed,
        QStringLiteral("The Admin .hscfg client identity is incomplete."));
  }

  QByteArray signedPayload = files.value(QStringLiteral("urls.yml"));
  signedPayload += files.value(QStringLiteral("key.yml"));
  signedPayload += config.caCertificatePem;
  const QByteArray computedHash =
      QCryptographicHash::hash(signedPayload, QCryptographicHash::Sha256)
          .toHex()
          .toLower();
  if (config.metadata.contentSha256.isEmpty()) {
    return failure(HscfgImportError::ContentHashMissing,
                   QStringLiteral("The Admin .hscfg content hash is missing."));
  }
  if (config.metadata.contentSha256 != QString::fromLatin1(computedHash)) {
    return failure(
        HscfgImportError::ContentHashMismatch,
        QStringLiteral("The Admin .hscfg content hash does not match."));
  }

  HscfgImportResult result;
  result.config = config;
  result.integrity = HscfgIntegrityState::ContentHashVerified;
  const QByteArray trustedKey = config.metadata.publicKey.isEmpty()
                                    ? parsePublicKey(m_trustedEd25519PublicKey)
                                    : config.metadata.publicKey;
  const QString algorithm = config.metadata.signatureAlgorithm.trimmed();
  if (algorithm.compare(QStringLiteral("Ed25519"), Qt::CaseInsensitive) == 0 &&
      !config.metadata.digitalSignature.isEmpty() && !trustedKey.isEmpty()) {
    if (!verifyEd25519(signedPayload, config.metadata.digitalSignature,
                       trustedKey)) {
      return failure(
          HscfgImportError::SignatureInvalid,
          QStringLiteral("The Admin .hscfg Ed25519 signature is invalid."));
    }
    result.config.metadata.publicKey = trustedKey;
    result.integrity = HscfgIntegrityState::FullyVerified;
  } else {
    result.integrity = HscfgIntegrityState::SignatureUnavailable;
    result.message = QStringLiteral(
        "The content hash is valid, but Ed25519 signature verification is "
        "unavailable because the package has no usable public key/signature.");
  }
  return result;
#endif
}

} // namespace HubSight::Admin
