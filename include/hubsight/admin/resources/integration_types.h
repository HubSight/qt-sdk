#pragma once

#include "../admin_export.h"

#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace HubSight::Admin {

struct HUBSIGHT_ADMIN_EXPORT ApiClient {
  QString id;
  QString clientId;
  QString name;
  QString description;
  QString type;
  QString status;
  QString key;
  QString secret;
  QString clientSecret;
  QStringList scopes;
  bool enabled = true;
  bool active = true;
  bool verified = false;
  QDateTime createdAt;
  QDateTime updatedAt;
  QDateTime lastUsedAt;
  QDateTime expiresAt;
  QJsonObject raw;

  bool isValid() const;
  static ApiClient fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT ApiClientPage {
  QVector<ApiClient> items;
  QString nextCursor;
  bool hasMore = false;
  QJsonObject raw;

  bool isValid() const;
  static ApiClientPage fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT GoogleServiceAccount {
  QString id;
  QString accountId;
  QString name;
  QString email;
  QString clientEmail;
  QString projectId;
  QString projectNumber;
  QString privateKeyId;
  QString type;
  QString status;
  bool active = false;
  bool enabled = true;
  bool firebaseReady = false;
  QDateTime createdAt;
  QDateTime updatedAt;
  QJsonObject credentials;
  QJsonObject raw;

  bool isValid() const;
  static GoogleServiceAccount fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT GoogleServiceAccountPage {
  QVector<GoogleServiceAccount> items;
  QString nextCursor;
  bool hasMore = false;
  QJsonObject raw;

  bool isValid() const;
  static GoogleServiceAccountPage fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT AppConfig {
  QString id;
  QString configId;
  QString name;
  QString platform;
  QString applicationId;
  QString packageName;
  QString bundleId;
  QString clientId;
  QString status;
  QString downloadUrl;
  QString qrCode;
  bool active = true;
  bool revoked = false;
  QDateTime createdAt;
  QDateTime updatedAt;
  QDateTime expiresAt;
  QJsonObject raw;

  bool isValid() const;
  static AppConfig fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT AppConfigPage {
  QVector<AppConfig> items;
  QString nextCursor;
  bool hasMore = false;
  QJsonObject raw;

  bool isValid() const;
  static AppConfigPage fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT IntegrationActionResult {
  QString id;
  QString resourceId;
  QString operationId;
  QString status;
  QString message;
  bool success = false;
  bool accepted = false;
  QJsonObject details;
  QJsonObject raw;

  bool isValid() const;
  static IntegrationActionResult fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT IntegrationUrlResult {
  QString id;
  QString resourceId;
  QString url;
  QString filename;
  QString contentType;
  QDateTime expiresAt;
  QJsonObject raw;

  bool isValid() const;
  static IntegrationUrlResult fromJson(const QJsonObject &json,
                                       const QString &resourceId = {});
};

struct HUBSIGHT_ADMIN_EXPORT IntegrationQrResult {
  QString id;
  QString resourceId;
  QString qrCode;
  QString data;
  QString format;
  QJsonObject raw;

  bool isValid() const;
  static IntegrationQrResult fromJson(const QJsonObject &json,
                                      const QString &resourceId = {});
};

struct HUBSIGHT_ADMIN_EXPORT FirebasePreflightResult {
  QString accountId;
  QString status;
  QString message;
  QStringList missing;
  bool ready = false;
  bool enabled = false;
  QJsonObject details;
  QJsonObject raw;

  bool isValid() const;
  static FirebasePreflightResult fromJson(const QJsonObject &json,
                                          const QString &accountId = {});
};

struct HUBSIGHT_ADMIN_EXPORT IntegrationOperation {
  QString id;
  QString operationId;
  QString name;
  QString type;
  QString status;
  QString message;
  double progress = 0.0;
  bool done = false;
  bool successful = false;
  bool cancelable = false;
  QDateTime createdAt;
  QDateTime updatedAt;
  QDateTime completedAt;
  QJsonObject response;
  QJsonObject error;
  QJsonObject metadata;
  QJsonObject raw;

  bool isValid() const;
  static IntegrationOperation fromJson(const QJsonObject &json);
};

// Short aliases make the DTOs convenient for callers while keeping their
// domain-specific names unambiguous in signal declarations.
using Operation = IntegrationOperation;
using UrlResult = IntegrationUrlResult;
using QrResult = IntegrationQrResult;

} // namespace HubSight::Admin

Q_DECLARE_METATYPE(HubSight::Admin::ApiClient)
Q_DECLARE_METATYPE(HubSight::Admin::ApiClientPage)
Q_DECLARE_METATYPE(HubSight::Admin::GoogleServiceAccount)
Q_DECLARE_METATYPE(HubSight::Admin::GoogleServiceAccountPage)
Q_DECLARE_METATYPE(HubSight::Admin::AppConfig)
Q_DECLARE_METATYPE(HubSight::Admin::AppConfigPage)
Q_DECLARE_METATYPE(HubSight::Admin::IntegrationActionResult)
Q_DECLARE_METATYPE(HubSight::Admin::IntegrationUrlResult)
Q_DECLARE_METATYPE(HubSight::Admin::IntegrationQrResult)
Q_DECLARE_METATYPE(HubSight::Admin::FirebasePreflightResult)
Q_DECLARE_METATYPE(HubSight::Admin::IntegrationOperation)
