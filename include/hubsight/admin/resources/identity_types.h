#pragma once

#include "../admin_export.h"

#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace HubSight::Admin {

struct HUBSIGHT_ADMIN_EXPORT Permission {
  QString id;
  QString key;
  QString name;
  QString description;
  QString resource;
  QString action;
  QString scope;
  bool system = false;
  bool active = true;
  QJsonObject raw;

  bool isValid() const;
  static Permission fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT PermissionPage {
  QVector<Permission> items;
  QString nextCursor;
  bool hasMore = false;
  QJsonObject raw;

  bool isValid() const;
  static PermissionPage fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT Role {
  QString id;
  QString name;
  QString description;
  QStringList permissions;
  QStringList permissionIds;
  QString scope;
  bool system = false;
  bool active = true;
  QDateTime createdAt;
  QDateTime updatedAt;
  QJsonObject raw;

  bool isValid() const;
  static Role fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT RolePage {
  QVector<Role> items;
  QString nextCursor;
  bool hasMore = false;
  QJsonObject raw;

  bool isValid() const;
  static RolePage fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT User {
  QString id;
  QString username;
  QString email;
  QString fullName;
  QString firstName;
  QString lastName;
  QString role;
  QString roleId;
  QStringList permissions;
  QString status;
  bool active = true;
  bool blocked = false;
  QDateTime createdAt;
  QDateTime updatedAt;
  QDateTime lastLoginAt;
  QJsonObject raw;

  bool isValid() const;
  static User fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT UserPage {
  QVector<User> items;
  QString nextCursor;
  bool hasMore = false;
  QJsonObject raw;

  bool isValid() const;
  static UserPage fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT UserSession {
  QString id;
  QString sessionId;
  QString userId;
  QString deviceName;
  QString platform;
  QString ipAddress;
  QString userAgent;
  QDateTime createdAt;
  QDateTime lastUsedAt;
  QDateTime expiresAt;
  bool current = false;
  bool revoked = false;
  QJsonObject raw;

  bool isValid() const;
  static UserSession fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT UserSessionPage {
  QVector<UserSession> items;
  QString nextCursor;
  bool hasMore = false;
  QJsonObject raw;

  bool isValid() const;
  static UserSessionPage fromJson(const QJsonObject &json);
};

// Mutation endpoints may return an empty 204 response. IdentityClient marks
// those successful responses locally while retaining any server payload.
struct HUBSIGHT_ADMIN_EXPORT IdentityActionResult {
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
  static IdentityActionResult fromJson(const QJsonObject &json);
};

} // namespace HubSight::Admin

Q_DECLARE_METATYPE(HubSight::Admin::Permission)
Q_DECLARE_METATYPE(HubSight::Admin::PermissionPage)
Q_DECLARE_METATYPE(HubSight::Admin::Role)
Q_DECLARE_METATYPE(HubSight::Admin::RolePage)
Q_DECLARE_METATYPE(HubSight::Admin::User)
Q_DECLARE_METATYPE(HubSight::Admin::UserPage)
Q_DECLARE_METATYPE(HubSight::Admin::UserSession)
Q_DECLARE_METATYPE(HubSight::Admin::UserSessionPage)
Q_DECLARE_METATYPE(HubSight::Admin::IdentityActionResult)
