#pragma once

#include "../admin_types.h"

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QUrl>

namespace HubSight::Admin {

struct HUBSIGHT_ADMIN_EXPORT HscfgUrls {
  QUrl gatewayUrl;
  QUrl apiBaseUrl;
  QUrl relayWebSocketUrl;
  QUrl webRtcBaseUrl;
  QUrl webRtcSignalingUrl;
  int webRtcMediaPort = 8555;

  bool isValid(QString *reason = nullptr) const;
};

struct HUBSIGHT_ADMIN_EXPORT HscfgClientIdentity {
  QString clientId;
  QString clientName;
  QString apiKey;
  QString platform;
  QString audience;
  QString authMode;

  bool isValid(QString *reason = nullptr) const;
};

struct HUBSIGHT_ADMIN_EXPORT HscfgMetadata {
  QString version;
  QString profile;
  QString apiNamespace;
  QString realtimeNamespace;
  bool fcmEnabled = false;
  QString contentSha256;
  QString signatureAlgorithm;
  QByteArray digitalSignature;
  QByteArray publicKey;
};

struct HUBSIGHT_ADMIN_EXPORT HscfgConfig {
  HscfgUrls urls;
  HscfgClientIdentity identity;
  HscfgMetadata metadata;
  QByteArray caCertificatePem;

  bool isValid(QString *reason = nullptr) const;
};

struct HUBSIGHT_ADMIN_EXPORT HscfgImportResult {
  HscfgConfig config;
  HscfgImportError error = HscfgImportError::None;
  HscfgIntegrityState integrity = HscfgIntegrityState::NotChecked;
  QString message;

  bool success() const { return error == HscfgImportError::None; }
};

HUBSIGHT_ADMIN_EXPORT QString toString(HscfgImportError error);
HUBSIGHT_ADMIN_EXPORT QString toString(HscfgIntegrityState state);

} // namespace HubSight::Admin

Q_DECLARE_METATYPE(HubSight::Admin::HscfgUrls)
Q_DECLARE_METATYPE(HubSight::Admin::HscfgClientIdentity)
Q_DECLARE_METATYPE(HubSight::Admin::HscfgMetadata)
Q_DECLARE_METATYPE(HubSight::Admin::HscfgConfig)
Q_DECLARE_METATYPE(HubSight::Admin::HscfgImportError)
Q_DECLARE_METATYPE(HubSight::Admin::HscfgIntegrityState)
Q_DECLARE_METATYPE(HubSight::Admin::HscfgImportResult)
