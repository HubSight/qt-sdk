#pragma once

#include "../admin_export.h"

#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QVector>

namespace HubSight::Admin {

struct HUBSIGHT_ADMIN_EXPORT Notification {
  QString id;
  QString cameraId;
  QString type;
  QString title;
  QString body;
  QString category;
  QString memberId;
  QString thumbnailUrl;
  bool isRead = false;
  QDateTime createdAt;
  QJsonObject raw;

  bool isValid() const;
  static Notification fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT NotificationPage {
  QVector<Notification> items;
  int unreadCount = 0;
  QString nextCursor;
  bool hasMore = false;
  QJsonObject raw;

  bool isValid() const;
  static NotificationPage fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT NotificationActionResult {
  QString status;
  int affectedCount = 0;
  QJsonObject raw;

  static NotificationActionResult fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT NotificationPushConfig {
  QString vapidPublicKey;
  QJsonObject firebase;
  QJsonObject raw;

  bool isValid() const;
  static NotificationPushConfig fromJson(const QJsonObject &json);
};

} // namespace HubSight::Admin

Q_DECLARE_METATYPE(HubSight::Admin::Notification)
Q_DECLARE_METATYPE(HubSight::Admin::NotificationPage)
Q_DECLARE_METATYPE(HubSight::Admin::NotificationActionResult)
Q_DECLARE_METATYPE(HubSight::Admin::NotificationPushConfig)
