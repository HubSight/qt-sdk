#include "../include/hubsight/admin/desktop_secure_storage.h"

#include <QByteArray>

#if defined(Q_OS_WIN) && defined(HUBSIGHT_ADMIN_WINDOWS_CREDENTIALS_AVAILABLE)
#include <wincred.h>
#include <windows.h>
#elif defined(Q_OS_MACOS) && defined(HUBSIGHT_ADMIN_MACOS_KEYCHAIN_AVAILABLE)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#elif defined(Q_OS_LINUX) && defined(HUBSIGHT_ADMIN_LIBSECRET_AVAILABLE)
#include <libsecret/secret.h>
#endif

namespace HubSight::Admin {
namespace {

#if defined(Q_OS_WIN) && defined(HUBSIGHT_ADMIN_WINDOWS_CREDENTIALS_AVAILABLE)
QString windowsError(DWORD code) {
  wchar_t *buffer = nullptr;
  const DWORD length = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
  const QString message =
      length > 0 && buffer
          ? QString::fromWCharArray(buffer, length).trimmed()
          : QStringLiteral("Windows Credential Manager error %1")
                .arg(static_cast<qulonglong>(code));
  if (buffer) {
    LocalFree(buffer);
  }
  return message;
}
#elif defined(Q_OS_MACOS) && defined(HUBSIGHT_ADMIN_MACOS_KEYCHAIN_AVAILABLE)
QString keychainError(OSStatus status) {
  CFStringRef description = SecCopyErrorMessageString(status, nullptr);
  if (!description) {
    return QStringLiteral("macOS Keychain error %1")
        .arg(static_cast<int>(status));
  }
  char buffer[512] = {};
  const bool converted = CFStringGetCString(description, buffer, sizeof(buffer),
                                            kCFStringEncodingUTF8);
  const QString result = converted ? QString::fromUtf8(buffer)
                                   : QStringLiteral("macOS Keychain error %1")
                                         .arg(static_cast<int>(status));
  CFRelease(description);
  return result;
}

CFStringRef makeCfString(const QString &value) {
  return CFStringCreateWithCharacters(
      nullptr, reinterpret_cast<const UniChar *>(value.utf16()), value.size());
}

CFMutableDictionaryRef keychainQuery(const QString &service,
                                     const QString &account) {
  CFMutableDictionaryRef query =
      CFDictionaryCreateMutable(nullptr, 4, &kCFTypeDictionaryKeyCallBacks,
                                &kCFTypeDictionaryValueCallBacks);
  CFStringRef serviceString = makeCfString(service);
  CFStringRef accountString = makeCfString(account);
  CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(query, kSecAttrService, serviceString);
  CFDictionarySetValue(query, kSecAttrAccount, accountString);
  CFRelease(serviceString);
  CFRelease(accountString);
  return query;
}
#endif

#if defined(Q_OS_LINUX) && defined(HUBSIGHT_ADMIN_LIBSECRET_AVAILABLE)
const SecretSchema kSecretSchema = {
    "com.hubsight.admin",
    SECRET_SCHEMA_NONE,
    {
        {"service", SECRET_SCHEMA_ATTRIBUTE_STRING},
        {"key", SECRET_SCHEMA_ATTRIBUTE_STRING},
        {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING},
    },
};
#endif

} // namespace

DesktopSecureStorage::DesktopSecureStorage(const QString &service)
    : m_service(service.trimmed().isEmpty()
                    ? QStringLiteral("com.hubsight.admin")
                    : service.trimmed()) {}

bool DesktopSecureStorage::isSupported() {
#if (defined(Q_OS_WIN) &&                                                      \
     defined(HUBSIGHT_ADMIN_WINDOWS_CREDENTIALS_AVAILABLE)) ||                 \
    (defined(Q_OS_MACOS) && defined(HUBSIGHT_ADMIN_MACOS_KEYCHAIN_AVAILABLE))
  return true;
#elif defined(Q_OS_LINUX) && defined(HUBSIGHT_ADMIN_LIBSECRET_AVAILABLE)
  return true;
#else
  return false;
#endif
}

QString DesktopSecureStorage::platformBackendName() {
#if defined(Q_OS_WIN) && defined(HUBSIGHT_ADMIN_WINDOWS_CREDENTIALS_AVAILABLE)
  return QStringLiteral("windows-credential-manager");
#elif defined(Q_OS_MACOS) && defined(HUBSIGHT_ADMIN_MACOS_KEYCHAIN_AVAILABLE)
  return QStringLiteral("macos-keychain");
#elif defined(Q_OS_LINUX) && defined(HUBSIGHT_ADMIN_LIBSECRET_AVAILABLE)
  return QStringLiteral("linux-secret-service");
#else
  return QStringLiteral("unsupported-desktop-platform");
#endif
}

QString DesktopSecureStorage::service() const { return m_service; }

QString DesktopSecureStorage::lastError() const { return m_lastError; }

std::optional<QByteArray> DesktopSecureStorage::read(const QString &key) const {
  m_lastError.clear();
  const QString normalized = normalizedKey(key);
  if (normalized.isEmpty()) {
    setError(QStringLiteral("Secure storage key must not be empty."));
    return std::nullopt;
  }

#if defined(Q_OS_WIN) && defined(HUBSIGHT_ADMIN_WINDOWS_CREDENTIALS_AVAILABLE)
  const std::wstring target =
      (m_service + QStringLiteral("/") + normalized).toStdWString();
  PCREDENTIALW credential = nullptr;
  if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
    const DWORD error = GetLastError();
    if (error == ERROR_NOT_FOUND) {
      return std::nullopt;
    }
    setError(windowsError(error));
    return std::nullopt;
  }
  QByteArray value;
  if (credential->CredentialBlob && credential->CredentialBlobSize > 0) {
    value =
        QByteArray(reinterpret_cast<const char *>(credential->CredentialBlob),
                   static_cast<int>(credential->CredentialBlobSize));
  }
  CredFree(credential);
  return value;
#elif defined(Q_OS_MACOS) && defined(HUBSIGHT_ADMIN_MACOS_KEYCHAIN_AVAILABLE)
  CFMutableDictionaryRef query = keychainQuery(m_service, normalized);
  CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
  CFTypeRef result = nullptr;
  const OSStatus status = SecItemCopyMatching(query, &result);
  CFRelease(query);
  if (status == errSecItemNotFound) {
    return std::nullopt;
  }
  if (status != errSecSuccess) {
    setError(keychainError(status));
    return std::nullopt;
  }
  if (!result || CFGetTypeID(result) != CFDataGetTypeID()) {
    if (result) {
      CFRelease(result);
    }
    setError(QStringLiteral("macOS Keychain returned a non-data item."));
    return std::nullopt;
  }
  const CFDataRef data = static_cast<CFDataRef>(result);
  const QByteArray value(reinterpret_cast<const char *>(CFDataGetBytePtr(data)),
                         static_cast<int>(CFDataGetLength(data)));
  CFRelease(result);
  return value;
#elif defined(Q_OS_LINUX) && defined(HUBSIGHT_ADMIN_LIBSECRET_AVAILABLE)
  GError *error = nullptr;
  const QByteArray serviceUtf8 = m_service.toUtf8();
  const QByteArray keyUtf8 = normalized.toUtf8();
  gchar *encoded = secret_password_lookup_sync(
      &kSecretSchema, nullptr, &error, "service", serviceUtf8.constData(),
      "key", keyUtf8.constData(), nullptr);
  if (error) {
    setError(QString::fromUtf8(error->message));
    g_error_free(error);
    return std::nullopt;
  }
  if (!encoded) {
    return std::nullopt;
  }
  const QByteArray value = QByteArray::fromBase64(QByteArray(encoded));
  secret_password_free(encoded);
  return value;
#else
  Q_UNUSED(normalized)
  setError(QStringLiteral(
      "No supported desktop credential-vault backend was compiled."));
  return std::nullopt;
#endif
}

bool DesktopSecureStorage::write(const QString &key, const QByteArray &value) {
  m_lastError.clear();
  const QString normalized = normalizedKey(key);
  if (normalized.isEmpty()) {
    setError(QStringLiteral("Secure storage key must not be empty."));
    return false;
  }

#if defined(Q_OS_WIN) && defined(HUBSIGHT_ADMIN_WINDOWS_CREDENTIALS_AVAILABLE)
  const std::wstring target =
      (m_service + QStringLiteral("/") + normalized).toStdWString();
  CREDENTIALW credential = {};
  credential.Type = CRED_TYPE_GENERIC;
  credential.TargetName = const_cast<LPWSTR>(target.c_str());
  credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
  credential.CredentialBlobSize = static_cast<DWORD>(value.size());
  credential.CredentialBlob =
      reinterpret_cast<LPBYTE>(const_cast<char *>(value.constData()));
  if (!CredWriteW(&credential, 0)) {
    setError(windowsError(GetLastError()));
    return false;
  }
  return true;
#elif defined(Q_OS_MACOS) && defined(HUBSIGHT_ADMIN_MACOS_KEYCHAIN_AVAILABLE)
  CFMutableDictionaryRef query = keychainQuery(m_service, normalized);
  CFDataRef data =
      CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(value.constData()),
                   value.size());
  CFMutableDictionaryRef attributes =
      CFDictionaryCreateMutable(nullptr, 1, &kCFTypeDictionaryKeyCallBacks,
                                &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(attributes, kSecValueData, data);
  OSStatus status = SecItemUpdate(query, attributes);
  if (status == errSecItemNotFound) {
    CFDictionarySetValue(query, kSecValueData, data);
    status = SecItemAdd(query, nullptr);
  }
  CFRelease(attributes);
  CFRelease(data);
  CFRelease(query);
  if (status != errSecSuccess) {
    setError(keychainError(status));
    return false;
  }
  return true;
#elif defined(Q_OS_LINUX) && defined(HUBSIGHT_ADMIN_LIBSECRET_AVAILABLE)
  GError *error = nullptr;
  const QByteArray serviceUtf8 = m_service.toUtf8();
  const QByteArray keyUtf8 = normalized.toUtf8();
  const QByteArray label =
      m_service.toUtf8() + QByteArrayLiteral(" refresh token");
  const QByteArray encoded = value.toBase64();
  const gboolean success = secret_password_store_sync(
      &kSecretSchema, SECRET_COLLECTION_DEFAULT, label.constData(),
      encoded.constData(), nullptr, &error, "service", serviceUtf8.constData(),
      "key", keyUtf8.constData(), nullptr);
  if (!success) {
    setError(error ? QString::fromUtf8(error->message)
                   : QStringLiteral("Secret Service rejected the credential."));
    if (error) {
      g_error_free(error);
    }
    return false;
  }
  return true;
#else
  Q_UNUSED(value)
  Q_UNUSED(normalized)
  setError(QStringLiteral(
      "No supported desktop credential-vault backend was compiled."));
  return false;
#endif
}

bool DesktopSecureStorage::remove(const QString &key) {
  m_lastError.clear();
  const QString normalized = normalizedKey(key);
  if (normalized.isEmpty()) {
    setError(QStringLiteral("Secure storage key must not be empty."));
    return false;
  }

#if defined(Q_OS_WIN) && defined(HUBSIGHT_ADMIN_WINDOWS_CREDENTIALS_AVAILABLE)
  const std::wstring target =
      (m_service + QStringLiteral("/") + normalized).toStdWString();
  if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) {
    const DWORD error = GetLastError();
    if (error == ERROR_NOT_FOUND) {
      return true;
    }
    setError(windowsError(error));
    return false;
  }
  return true;
#elif defined(Q_OS_MACOS) && defined(HUBSIGHT_ADMIN_MACOS_KEYCHAIN_AVAILABLE)
  CFMutableDictionaryRef query = keychainQuery(m_service, normalized);
  const OSStatus status = SecItemDelete(query);
  CFRelease(query);
  if (status == errSecSuccess || status == errSecItemNotFound) {
    return true;
  }
  setError(keychainError(status));
  return false;
#elif defined(Q_OS_LINUX) && defined(HUBSIGHT_ADMIN_LIBSECRET_AVAILABLE)
  GError *error = nullptr;
  const QByteArray serviceUtf8 = m_service.toUtf8();
  const QByteArray keyUtf8 = normalized.toUtf8();
  const gboolean success = secret_password_clear_sync(
      &kSecretSchema, nullptr, &error, "service", serviceUtf8.constData(),
      "key", keyUtf8.constData(), nullptr);
  if (!success && error) {
    setError(QString::fromUtf8(error->message));
    g_error_free(error);
    return false;
  }
  return true;
#else
  Q_UNUSED(normalized)
  setError(QStringLiteral(
      "No supported desktop credential-vault backend was compiled."));
  return false;
#endif
}

QString DesktopSecureStorage::backendName() const {
  return platformBackendName();
}

void DesktopSecureStorage::setError(const QString &message) const {
  m_lastError = message;
}

QString DesktopSecureStorage::normalizedKey(const QString &key) const {
  return key.trimmed();
}

} // namespace HubSight::Admin
