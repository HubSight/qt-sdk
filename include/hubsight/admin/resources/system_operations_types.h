#pragma once

#include "../admin_export.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QVector>

namespace HubSight::Admin {

struct HUBSIGHT_ADMIN_EXPORT DashboardSummary {
  QString status;
  QDateTime generatedAt;
  QJsonObject counts;
  QJsonObject metrics;
  QJsonObject cameras;
  QJsonObject members;
  QJsonObject notifications;
  QJsonObject storage;
  QJsonObject system;
  QString requestId;
  QJsonObject raw;

  bool isValid() const;
  static DashboardSummary fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT DashboardActivityItem {
  QString id;
  QString type;
  QString action;
  QString title;
  QString message;
  QString actorId;
  QString actorName;
  QString entityType;
  QString entityId;
  QDateTime occurredAt;
  QJsonObject metadata;
  QJsonObject raw;

  bool isValid() const;
  static DashboardActivityItem fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT DashboardActivity {
  QVector<DashboardActivityItem> items;
  QString nextCursor;
  bool hasMore = false;
  QJsonObject raw;

  bool isValid() const;
  static DashboardActivity fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT SystemHealth {
  QString status;
  QString version;
  bool healthy = false;
  double uptimeSeconds = 0.0;
  QDateTime checkedAt;
  QJsonObject checks;
  QJsonObject services;
  QJsonObject details;
  QString requestId;
  QJsonObject raw;

  bool isValid() const;
  static SystemHealth fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT SystemSettings {
  QJsonObject values;
  QString requestId;
  QJsonObject raw;

  bool isValid() const;
  static SystemSettings fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT SystemSettingsPatchResult {
  SystemSettings settings;
  QString status;
  QString message;
  QString operationId;
  QJsonObject changes;
  bool success = false;
  bool accepted = false;
  QJsonObject raw;

  static SystemSettingsPatchResult fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT StorageCleanupResult {
  QString status;
  QString message;
  QString operationId;
  qint64 deletedFiles = 0;
  qint64 deletedBytes = 0;
  qint64 freedBytes = 0;
  bool success = false;
  bool accepted = false;
  QJsonObject details;
  QJsonObject raw;

  static StorageCleanupResult fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT AuditEvent {
  QString id;
  QString action;
  QString category;
  QString type;
  QString actorId;
  QString actorName;
  QString actorType;
  QString resourceType;
  QString resourceId;
  QString ipAddress;
  QString userAgent;
  QDateTime occurredAt;
  QJsonObject details;
  QJsonObject metadata;
  QJsonObject raw;

  bool isValid() const;
  static AuditEvent fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT AuditEventPage {
  QVector<AuditEvent> items;
  QString nextCursor;
  bool hasMore = false;
  QJsonObject raw;

  bool isValid() const;
  static AuditEventPage fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT NvrStatus {
  QString status;
  QString state;
  QString host;
  QString version;
  bool online = false;
  bool connected = false;
  int cameraCount = 0;
  int streamCount = 0;
  QDateTime updatedAt;
  QJsonObject details;
  QJsonObject raw;

  bool isValid() const;
  static NvrStatus fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT PoolStatus {
  QString status;
  QString state;
  bool healthy = false;
  int totalNodes = 0;
  int onlineNodes = 0;
  int offlineNodes = 0;
  QDateTime updatedAt;
  QJsonArray nodes;
  QJsonObject details;
  QJsonObject raw;

  bool isValid() const;
  static PoolStatus fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT PoolSyncResult {
  QString status;
  QString message;
  QString operationId;
  int synchronizedCount = 0;
  bool success = false;
  bool accepted = false;
  QJsonObject details;
  QJsonObject raw;

  static PoolSyncResult fromJson(const QJsonObject &json);
};

// Endpoint-oriented aliases keep the DTOs usable with both catalog vocabulary
// and the shorter names commonly used by applications.
using DashboardActivityPage = DashboardActivity;
using DashboardActivityEvent = DashboardActivityItem;
using SystemAuditEvent = AuditEvent;
using SystemAuditEventPage = AuditEventPage;
using SystemStorageCleanupResult = StorageCleanupResult;
using NVRStatus = NvrStatus;

} // namespace HubSight::Admin

Q_DECLARE_METATYPE(HubSight::Admin::DashboardSummary)
Q_DECLARE_METATYPE(HubSight::Admin::DashboardActivityItem)
Q_DECLARE_METATYPE(HubSight::Admin::DashboardActivity)
Q_DECLARE_METATYPE(HubSight::Admin::SystemHealth)
Q_DECLARE_METATYPE(HubSight::Admin::SystemSettings)
Q_DECLARE_METATYPE(HubSight::Admin::SystemSettingsPatchResult)
Q_DECLARE_METATYPE(HubSight::Admin::StorageCleanupResult)
Q_DECLARE_METATYPE(HubSight::Admin::AuditEvent)
Q_DECLARE_METATYPE(HubSight::Admin::AuditEventPage)
Q_DECLARE_METATYPE(HubSight::Admin::NvrStatus)
Q_DECLARE_METATYPE(HubSight::Admin::PoolStatus)
Q_DECLARE_METATYPE(HubSight::Admin::PoolSyncResult)
