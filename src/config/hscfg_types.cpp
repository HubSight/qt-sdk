#include "../../include/hubsight/admin/config/hscfg_types.h"

namespace HubSight::Admin {
namespace {

bool fail(QString *reason, const QString &message) {
  if (reason) {
    *reason = message;
  }
  return false;
}

} // namespace

bool HscfgUrls::isValid(QString *reason) const {
  if (!gatewayUrl.isValid() || gatewayUrl.host().isEmpty() ||
      !apiBaseUrl.isValid() || apiBaseUrl.host().isEmpty() ||
      !relayWebSocketUrl.isValid() || relayWebSocketUrl.host().isEmpty() ||
      !webRtcBaseUrl.isValid() || webRtcBaseUrl.host().isEmpty() ||
      !webRtcSignalingUrl.isValid() || webRtcSignalingUrl.host().isEmpty()) {
    return fail(reason, QStringLiteral("The .hscfg URL set is incomplete."));
  }
  if (webRtcMediaPort < 1 || webRtcMediaPort > 65535) {
    return fail(reason, QStringLiteral("The WebRTC media port is invalid."));
  }
  return true;
}

bool HscfgClientIdentity::isValid(QString *reason) const {
  if (clientId.trimmed().isEmpty() || apiKey.trimmed().isEmpty()) {
    return fail(
        reason,
        QStringLiteral("The Admin .hscfg client ID and API key are required."));
  }
  if (platform != QStringLiteral("admin_desktop") ||
      audience != QStringLiteral("admin_api") ||
      authMode != QStringLiteral("bearer_jwt_plus_api_key")) {
    return fail(
        reason,
        QStringLiteral("The .hscfg identity is not an Admin API identity."));
  }
  return true;
}

bool HscfgConfig::isValid(QString *reason) const {
  QString detail;
  if (!urls.isValid(&detail)) {
    return fail(reason, detail);
  }
  if (!identity.isValid(&detail)) {
    return fail(reason, detail);
  }
  if (metadata.version != QStringLiteral("2.0") ||
      metadata.profile != QStringLiteral("admin_api") ||
      metadata.apiNamespace != QStringLiteral("/api/admin/v1") ||
      metadata.realtimeNamespace != QStringLiteral("/relay/admin/v1") ||
      metadata.fcmEnabled) {
    return fail(reason,
                QStringLiteral(
                    "The .hscfg metadata is not a valid Admin API profile."));
  }
  return true;
}

QString toString(HscfgImportError error) {
  switch (error) {
  case HscfgImportError::None:
    return QStringLiteral("none");
  case HscfgImportError::ImporterUnavailable:
    return QStringLiteral("importer_unavailable");
  case HscfgImportError::InvalidMagic:
    return QStringLiteral("invalid_magic");
  case HscfgImportError::InvalidContainer:
    return QStringLiteral("invalid_container");
  case HscfgImportError::InvalidPin:
    return QStringLiteral("invalid_pin");
  case HscfgImportError::DecryptionFailed:
    return QStringLiteral("decryption_failed");
  case HscfgImportError::ZipInvalid:
    return QStringLiteral("zip_invalid");
  case HscfgImportError::YamlInvalid:
    return QStringLiteral("yaml_invalid");
  case HscfgImportError::UnexpectedPayload:
    return QStringLiteral("unexpected_payload");
  case HscfgImportError::ValidationFailed:
    return QStringLiteral("validation_failed");
  case HscfgImportError::ContentHashMissing:
    return QStringLiteral("content_hash_missing");
  case HscfgImportError::ContentHashMismatch:
    return QStringLiteral("content_hash_mismatch");
  case HscfgImportError::SignatureInvalid:
    return QStringLiteral("signature_invalid");
  }
  return QStringLiteral("unknown");
}

QString toString(HscfgIntegrityState state) {
  switch (state) {
  case HscfgIntegrityState::NotChecked:
    return QStringLiteral("not_checked");
  case HscfgIntegrityState::ContentHashVerified:
    return QStringLiteral("content_hash_verified");
  case HscfgIntegrityState::SignatureUnavailable:
    return QStringLiteral("signature_unavailable");
  case HscfgIntegrityState::FullyVerified:
    return QStringLiteral("fully_verified");
  }
  return QStringLiteral("not_checked");
}

} // namespace HubSight::Admin
