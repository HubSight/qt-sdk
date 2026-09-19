#include "../../include/hubsight/admin/resources/notification_types.h"

#include <QJsonArray>

#include <initializer_list>

namespace HubSight::Admin {
namespace {

QJsonObject objectPayload(const QJsonObject &json) {
  const QJsonValue data = json.value(QStringLiteral("data"));
  return data.isObject() ? data.toObject() : json;
}

QString firstString(const QJsonObject &json,
                    std::initializer_list<const char *> keys) {
  for (const char *key : keys) {
    const QString value = json.value(QString::fromLatin1(key)).toString();
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

int firstInt(const QJsonObject &json, std::initializer_list<const char *> keys,
             int fallback = 0) {
  for (const char *key : keys) {
    const QJsonValue value = json.value(QString::fromLatin1(key));
    if (value.isDouble()) {
      return static_cast<int>(value.toDouble());
    }
    if (value.isString()) {
      bool ok = false;
      const int result = value.toString().toInt(&ok);
      if (ok) {
        return result;
      }
    }
  }
  return fallback;
}

QDateTime timestamp(const QJsonValue &value) {
  if (!value.isString()) {
    return {};
  }
  const QString input = value.toString();
  QDateTime result = QDateTime::fromString(input, Qt::ISODateWithMs);
  if (!result.isValid()) {
    result = QDateTime::fromString(input, Qt::ISODate);
  }
  return result.isValid() ? result.toUTC() : QDateTime{};
}

QDateTime firstTimestamp(const QJsonObject &json,
                         std::initializer_list<const char *> keys) {
  for (const char *key : keys) {
    const QDateTime value = timestamp(json.value(QString::fromLatin1(key)));
    if (value.isValid()) {
      return value;
    }
  }
  return {};
}

QJsonArray arrayValue(const QJsonObject &json,
                      std::initializer_list<const char *> keys) {
  for (const char *key : keys) {
    const QJsonValue value = json.value(QString::fromLatin1(key));
    if (value.isArray()) {
      return value.toArray();
    }
  }
  return {};
}

} // namespace

bool Notification::isValid() const { return !id.trimmed().isEmpty(); }

Notification Notification::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  Notification result;
  result.id = firstString(source, {"id", "notification_id", "notificationId"});
  result.cameraId = firstString(source, {"camera_id", "cameraId"});
  result.type = source.value(QStringLiteral("type")).toString();
  result.title = source.value(QStringLiteral("title")).toString();
  result.body = source.value(QStringLiteral("body")).toString();
  result.category = source.value(QStringLiteral("category")).toString();
  result.memberId = firstString(source, {"member_id", "memberId"});
  result.thumbnailUrl = firstString(source, {"thumbnail_url", "thumbnailUrl"});
  result.isRead = firstBool(source, {"is_read", "isRead", "read"}, false);
  result.createdAt =
      firstTimestamp(source, {"created_at", "createdAt", "timestamp"});
  result.raw = source;
  return result;
}

bool NotificationPage::isValid() const {
  if (unreadCount < 0) {
    return false;
  }
  for (const Notification &item : items) {
    if (!item.isValid()) {
      return false;
    }
  }
  return !raw.isEmpty();
}

NotificationPage NotificationPage::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  NotificationPage result;
  const QJsonArray values =
      arrayValue(source, {"notifications", "items", "data"});
  result.items.reserve(values.size());
  for (const QJsonValue &value : values) {
    if (value.isObject()) {
      result.items.append(Notification::fromJson(value.toObject()));
    }
  }
  result.unreadCount =
      firstInt(source, {"unread_count", "unreadCount", "unread"});
  result.nextCursor =
      firstString(source, {"next_cursor", "nextCursor", "cursor"});
  const QJsonValue hasMore = source.value(QStringLiteral("has_more"));
  result.hasMore = hasMore.isUndefined()
                       ? source.value(QStringLiteral("hasMore"))
                             .toBool(!result.nextCursor.isEmpty())
                       : hasMore.toBool(!result.nextCursor.isEmpty());
  result.raw = source;
  return result;
}

NotificationActionResult
NotificationActionResult::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  NotificationActionResult result;
  result.status = firstString(source, {"status", "result"});
  result.affectedCount =
      firstInt(source, {"affected", "affected_count", "affectedCount",
                        "deleted", "count"});
  result.raw = source;
  return result;
}

bool NotificationPushConfig::isValid() const { return !raw.isEmpty(); }

NotificationPushConfig
NotificationPushConfig::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  NotificationPushConfig result;
  result.vapidPublicKey =
      firstString(source, {"vapid_public_key", "vapidPublicKey", "public_key",
                           "publicKey"});
  result.firebase = source.value(QStringLiteral("firebase")).toObject();
  result.raw = source;
  return result;
}

} // namespace HubSight::Admin
