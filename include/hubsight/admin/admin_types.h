#pragma once

#include "admin_export.h"

#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace HubSight::Admin {

Q_NAMESPACE

enum class AdminState {
  Unconfigured,
  Unauthenticated,
  Authenticating,
  Ready,
  Maintenance,
  Offline,
  Revoked,
};
Q_ENUM_NS(AdminState)

enum class HttpProtocol {
  Unknown,
  Http1_1,
  Http2,
};
Q_ENUM_NS(HttpProtocol)

enum class ErrorCategory {
  Configuration,
  Authentication,
  Authorization,
  Maintenance,
  Validation,
  Conflict,
  Network,
  Server,
  Parse,
  Canceled,
  Unknown,
};
Q_ENUM_NS(ErrorCategory)

struct HUBSIGHT_ADMIN_EXPORT MaintenanceInfo {
  bool active = true;
  QString code;
  QString requestId;
  int retryAfterSeconds = 300;
};

struct HUBSIGHT_ADMIN_EXPORT AdminError {
  ErrorCategory category = ErrorCategory::Unknown;
  int httpStatus = 0;
  QString serverCode;
  QString developerMessage;
  QString requestId;
  QJsonValue details;
  QString operation;
  int retryAfterSeconds = 0;
  bool retryable = false;

  bool isMaintenance() const;
  bool isAuthenticationError() const;
  bool isNetworkError() const;
};

struct HUBSIGHT_ADMIN_EXPORT TokenSet {
  QString accessToken;
  QString refreshToken;
  QString tokenType = QStringLiteral("Bearer");
  int expiresInSeconds = 0;
  QString clientId;

  bool isValid() const;
  static TokenSet fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT AdminUser {
  QString id;
  QString username;
  QString fullName;
  QString role;
  QString locale;
  QString timezone;
  QStringList permissions;
  bool active = true;

  bool isValid() const;
  static AdminUser fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT SystemStatus {
  QString apiVersion;
  bool adminApiEnabled = false;
  QDateTime serverTime;
  QJsonObject features;
  QString requestId;

  static SystemStatus fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT Capabilities {
  int matrixLimit = 0;
  QStringList supportedMatrixProfiles;
  QString realtimeTransport;
  QStringList authentication;
  QString mediaTransport;
  QString requestId;

  static Capabilities fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT CameraSummary {
  QString id;
  QString name;
  QString host;
  QString brand;
  int rtspPort = 0;
  QString rtspTransport;
  bool active = false;
  bool stopped = false;
  bool aiEnabled = false;
  bool showBbox = false;
  QString nvrMode;
  QString recordQuality;
  bool fixed = false;
  bool homographyValid = false;
  bool onvifEnabled = false;
  bool ptzSupported = false;
  QDateTime createdAt;
  QDateTime updatedAt;

  bool isValid() const;
  static CameraSummary fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT CameraPage {
  QVector<CameraSummary> items;
  QString nextCursor;
  bool hasMore = false;
};

} // namespace HubSight::Admin

Q_DECLARE_METATYPE(HubSight::Admin::AdminState)
Q_DECLARE_METATYPE(HubSight::Admin::HttpProtocol)
Q_DECLARE_METATYPE(HubSight::Admin::ErrorCategory)
Q_DECLARE_METATYPE(HubSight::Admin::MaintenanceInfo)
Q_DECLARE_METATYPE(HubSight::Admin::AdminError)
Q_DECLARE_METATYPE(HubSight::Admin::TokenSet)
Q_DECLARE_METATYPE(HubSight::Admin::AdminUser)
Q_DECLARE_METATYPE(HubSight::Admin::SystemStatus)
Q_DECLARE_METATYPE(HubSight::Admin::Capabilities)
Q_DECLARE_METATYPE(HubSight::Admin::CameraSummary)
Q_DECLARE_METATYPE(HubSight::Admin::CameraPage)
