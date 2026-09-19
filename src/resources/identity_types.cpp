#include "../../include/hubsight/admin/resources/identity_types.h"

#include <QJsonArray>
#include <QJsonValue>

#include <initializer_list>

namespace HubSight::Admin {
namespace {

QJsonObject objectPayload(const QJsonObject &json) {
  const QJsonValue data = json.value(QStringLiteral("data"));
  return data.isObject() ? data.toObject() : json;
}

QJsonObject nestedObject(const QJsonObject &json,
                         std::initializer_list<const char *> keys) {
  for (const char *key : keys) {
    const QJsonValue value = json.value(QString::fromLatin1(key));
    if (value.isObject()) {
      return value.toObject();
    }
  }
  return {};
}

QJsonObject resourceObject(const QJsonObject &json,
                           std::initializer_list<const char *> keys) {
  const QJsonObject source = objectPayload(json);
  const QJsonObject nested = nestedObject(source, keys);
  return nested.isEmpty() ? source : nested;
}

QString stringValue(const QJsonValue &value) {
  if (value.isString()) {
    return value.toString();
  }
  if (value.isDouble()) {
    return QString::number(value.toDouble(), 'g', 15);
  }
  return {};
}

QString firstString(const QJsonObject &json,
                    std::initializer_list<const char *> keys) {
  for (const char *key : keys) {
    const QString value = stringValue(json.value(QString::fromLatin1(key)));
    if (!value.isEmpty()) {
      return value;
    }
  }
  return {};
}

bool firstBool(const QJsonObject &json,
               std::initializer_list<const char *> keys, bool fallback) {
  for (const char *key : keys) {
    const QString name = QString::fromLatin1(key);
    if (json.contains(name)) {
      return json.value(name).toBool(fallback);
    }
  }
  return fallback;
}

QStringList stringList(const QJsonValue &value) {
  QStringList result;
  if (!value.isArray()) {
    return result;
  }
  for (const QJsonValue &item : value.toArray()) {
    if (item.isString() || item.isDouble()) {
      const QString text = stringValue(item);
      if (!text.isEmpty()) {
        result.append(text);
      }
    } else if (item.isObject()) {
      const QJsonObject object = item.toObject();
      const QString text =
          firstString(object, {"key", "id", "name", "permission", "code"});
      if (!text.isEmpty()) {
        result.append(text);
      }
    }
  }
  return result;
}

QStringList firstStringList(const QJsonObject &json,
                            std::initializer_list<const char *> keys) {
  for (const char *key : keys) {
    const QStringList result = stringList(json.value(QString::fromLatin1(key)));
    if (!result.isEmpty()) {
      return result;
    }
  }
  return {};
}

QJsonArray firstArray(const QJsonObject &json,
                      std::initializer_list<const char *> keys) {
  for (const char *key : keys) {
    const QJsonValue value = json.value(QString::fromLatin1(key));
    if (value.isArray()) {
      return value.toArray();
    }
  }
  return {};
}

QDateTime timestamp(const QJsonValue &value) {
  if (!value.isString()) {
    return {};
  }
  QString input = value.toString();
  const int dot = input.indexOf(QLatin1Char('.'));
  if (dot >= 0) {
    int fractionEnd = input.size();
    for (int index = dot + 1; index < input.size(); ++index) {
      const QChar character = input.at(index);
      if (character == QLatin1Char('Z') || character == QLatin1Char('+') ||
          character == QLatin1Char('-')) {
        fractionEnd = index;
        break;
      }
    }
    QString fraction = input.mid(dot + 1, fractionEnd - dot - 1).left(3);
    while (fraction.size() < 3) {
      fraction.append(QLatin1Char('0'));
    }
    input = input.left(dot + 1) + fraction + input.mid(fractionEnd);
  }
  QDateTime result = QDateTime::fromString(input, Qt::ISODateWithMs);
  if (!result.isValid()) {
    result = QDateTime::fromString(input, Qt::ISODate);
  }
  return result.isValid() ? result.toUTC() : QDateTime{};
}

QDateTime firstTimestamp(const QJsonObject &json,
                         std::initializer_list<const char *> keys) {
  for (const char *key : keys) {
    const QDateTime result = timestamp(json.value(QString::fromLatin1(key)));
    if (result.isValid()) {
      return result;
    }
  }
  return {};
}

} // namespace

bool Permission::isValid() const {
  return !id.trimmed().isEmpty() || !key.trimmed().isEmpty() ||
         !name.trimmed().isEmpty();
}

Permission Permission::fromJson(const QJsonObject &json) {
  const QJsonObject source = resourceObject(json, {"permission"});
  Permission result;
  result.id = firstString(source, {"id", "permission_id", "permissionId"});
  result.key = firstString(source, {"key", "permission", "code", "slug"});
  result.name = firstString(source, {"name", "display_name", "displayName"});
  if (result.name.isEmpty()) {
    result.name = result.key;
  }
  result.description = firstString(source, {"description", "description_en"});
  result.resource = firstString(source, {"resource", "resource_name"});
  result.action = firstString(source, {"action", "verb"});
  result.scope = firstString(source, {"scope", "permission_scope"});
  result.system = firstBool(source, {"system", "is_system"}, false);
  result.active = firstBool(source, {"active", "is_active", "enabled"}, true);
  result.raw = source;
  return result;
}

bool PermissionPage::isValid() const {
  for (const Permission &item : items) {
    if (!item.isValid()) {
      return false;
    }
  }
  return !raw.isEmpty();
}

PermissionPage PermissionPage::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  PermissionPage result;
  const QJsonArray values =
      firstArray(source, {"permissions", "items", "data"});
  result.items.reserve(values.size());
  for (const QJsonValue &value : values) {
    if (value.isObject()) {
      result.items.append(Permission::fromJson(value.toObject()));
    } else if (value.isString() || value.isDouble()) {
      const QString key = stringValue(value);
      result.items.append(
          Permission::fromJson(QJsonObject{{QStringLiteral("id"), key},
                                           {QStringLiteral("key"), key},
                                           {QStringLiteral("name"), key}}));
    }
  }
  result.nextCursor =
      firstString(source, {"next_cursor", "nextCursor", "cursor"});
  result.hasMore = source.contains(QStringLiteral("has_more"))
                       ? source.value(QStringLiteral("has_more")).toBool()
                       : source.value(QStringLiteral("hasMore"))
                             .toBool(!result.nextCursor.isEmpty());
  result.raw = source;
  return result;
}

bool Role::isValid() const {
  return !id.trimmed().isEmpty() || !name.trimmed().isEmpty() || !raw.isEmpty();
}

Role Role::fromJson(const QJsonObject &json) {
  const QJsonObject source = resourceObject(json, {"role"});
  Role result;
  result.id = firstString(source, {"id", "role_id", "roleId"});
  result.name = firstString(source, {"name", "display_name", "displayName"});
  result.description = firstString(source, {"description", "description_en"});
  result.permissions =
      firstStringList(source, {"permissions", "permission_keys"});
  result.permissionIds = firstStringList(
      source, {"permission_ids", "permissionIds", "permissions_ids"});
  if (result.permissionIds.isEmpty() && !result.permissions.isEmpty()) {
    result.permissionIds = result.permissions;
  }
  result.scope = firstString(source, {"scope", "role_scope"});
  result.system = firstBool(source, {"system", "is_system"}, false);
  result.active = firstBool(source, {"active", "is_active", "enabled"}, true);
  result.createdAt = firstTimestamp(source, {"created_at", "createdAt"});
  result.updatedAt = firstTimestamp(source, {"updated_at", "updatedAt"});
  result.raw = source;
  return result;
}

bool RolePage::isValid() const {
  for (const Role &item : items) {
    if (!item.isValid()) {
      return false;
    }
  }
  return !raw.isEmpty();
}

RolePage RolePage::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  RolePage result;
  const QJsonArray values = firstArray(source, {"roles", "items", "data"});
  result.items.reserve(values.size());
  for (const QJsonValue &value : values) {
    if (value.isObject()) {
      result.items.append(Role::fromJson(value.toObject()));
    }
  }
  result.nextCursor =
      firstString(source, {"next_cursor", "nextCursor", "cursor"});
  result.hasMore = source.contains(QStringLiteral("has_more"))
                       ? source.value(QStringLiteral("has_more")).toBool()
                       : source.value(QStringLiteral("hasMore"))
                             .toBool(!result.nextCursor.isEmpty());
  result.raw = source;
  return result;
}

bool User::isValid() const {
  return !id.trimmed().isEmpty() || !username.trimmed().isEmpty() ||
         !email.trimmed().isEmpty() || !raw.isEmpty();
}

User User::fromJson(const QJsonObject &json) {
  const QJsonObject source = resourceObject(json, {"user", "member"});
  User result;
  result.id = firstString(source, {"id", "user_id", "userId"});
  result.username = firstString(source, {"username", "login", "user_name"});
  result.email =
      firstString(source, {"email", "email_address", "emailAddress"});
  result.fullName = firstString(source, {"full_name", "fullName", "name"});
  result.firstName = firstString(source, {"first_name", "firstName"});
  result.lastName = firstString(source, {"last_name", "lastName"});
  result.role = firstString(source, {"role", "role_name", "roleName"});
  if (result.role.isEmpty()) {
    result.role = firstString(source.value(QStringLiteral("role")).toObject(),
                              {"name", "id"});
  }
  result.roleId = firstString(source, {"role_id", "roleId"});
  if (result.roleId.isEmpty()) {
    result.roleId = firstString(source.value(QStringLiteral("role")).toObject(),
                                {"id", "role_id", "roleId"});
  }
  result.permissions = firstStringList(source, {"permissions"});
  result.status = firstString(source, {"status", "user_status"});
  result.active = firstBool(source, {"active", "is_active", "enabled"}, true);
  result.blocked = firstBool(source, {"blocked", "is_blocked"},
                             result.status.compare(QStringLiteral("blocked"),
                                                   Qt::CaseInsensitive) == 0);
  result.createdAt = firstTimestamp(source, {"created_at", "createdAt"});
  result.updatedAt = firstTimestamp(source, {"updated_at", "updatedAt"});
  result.lastLoginAt =
      firstTimestamp(source, {"last_login_at", "lastLoginAt", "last_login"});
  result.raw = source;
  return result;
}

bool UserPage::isValid() const {
  for (const User &item : items) {
    if (!item.isValid()) {
      return false;
    }
  }
  return !raw.isEmpty();
}

UserPage UserPage::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  UserPage result;
  const QJsonArray values = firstArray(source, {"users", "items", "data"});
  result.items.reserve(values.size());
  for (const QJsonValue &value : values) {
    if (value.isObject()) {
      result.items.append(User::fromJson(value.toObject()));
    }
  }
  result.nextCursor =
      firstString(source, {"next_cursor", "nextCursor", "cursor"});
  result.hasMore = source.contains(QStringLiteral("has_more"))
                       ? source.value(QStringLiteral("has_more")).toBool()
                       : source.value(QStringLiteral("hasMore"))
                             .toBool(!result.nextCursor.isEmpty());
  result.raw = source;
  return result;
}

bool UserSession::isValid() const {
  return !id.trimmed().isEmpty() || !sessionId.trimmed().isEmpty() ||
         !raw.isEmpty();
}

UserSession UserSession::fromJson(const QJsonObject &json) {
  const QJsonObject source = resourceObject(json, {"session"});
  UserSession result;
  result.id = firstString(source, {"id", "session_id", "sessionId"});
  result.sessionId = firstString(source, {"session_id", "sessionId", "id"});
  result.userId = firstString(source, {"user_id", "userId"});
  result.deviceName =
      firstString(source, {"device_name", "deviceName", "name"});
  result.platform = firstString(source, {"platform", "os"});
  result.ipAddress = firstString(source, {"ip_address", "ipAddress", "ip"});
  result.userAgent = firstString(source, {"user_agent", "userAgent"});
  result.createdAt = firstTimestamp(source, {"created_at", "createdAt"});
  result.lastUsedAt = firstTimestamp(
      source, {"last_used_at", "lastUsedAt", "last_active_at", "lastActiveAt"});
  result.expiresAt = firstTimestamp(source, {"expires_at", "expiresAt"});
  result.current = firstBool(source, {"current", "is_current"}, false);
  result.revoked = firstBool(source, {"revoked", "is_revoked"}, false);
  result.raw = source;
  return result;
}

bool UserSessionPage::isValid() const {
  for (const UserSession &item : items) {
    if (!item.isValid()) {
      return false;
    }
  }
  return !raw.isEmpty();
}

UserSessionPage UserSessionPage::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  UserSessionPage result;
  const QJsonArray values =
      firstArray(source, {"sessions", "user_sessions", "items", "data"});
  result.items.reserve(values.size());
  for (const QJsonValue &value : values) {
    if (value.isObject()) {
      result.items.append(UserSession::fromJson(value.toObject()));
    }
  }
  result.nextCursor =
      firstString(source, {"next_cursor", "nextCursor", "cursor"});
  result.hasMore = source.contains(QStringLiteral("has_more"))
                       ? source.value(QStringLiteral("has_more")).toBool()
                       : source.value(QStringLiteral("hasMore"))
                             .toBool(!result.nextCursor.isEmpty());
  result.raw = source;
  return result;
}

bool IdentityActionResult::isValid() const {
  return success || accepted || !status.isEmpty() || !message.isEmpty() ||
         !id.isEmpty() || !operationId.isEmpty() || !raw.isEmpty();
}

IdentityActionResult IdentityActionResult::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  IdentityActionResult result;
  result.id = firstString(source, {"id", "user_id", "role_id", "session_id"});
  result.resourceId = firstString(source, {"resource_id", "resourceId"});
  result.operationId =
      firstString(source, {"operation_id", "operationId", "job_id", "jobId"});
  result.status = firstString(source, {"status", "result"});
  result.message = firstString(source, {"message", "message_en"});
  result.success = firstBool(source, {"success", "ok"}, false);
  result.accepted = firstBool(source, {"accepted", "queued"}, false);
  result.details = nestedObject(source, {"details", "result"});
  result.raw = source;
  return result;
}

} // namespace HubSight::Admin
