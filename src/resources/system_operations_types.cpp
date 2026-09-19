#include "../../include/hubsight/admin/resources/system_operations_types.h"

#include <initializer_list>

namespace HubSight::Admin {
namespace {

QJsonObject objectPayload(const QJsonObject &json) {
  const QJsonValue data = json.value(QStringLiteral("data"));
  return data.isObject() ? data.toObject() : json;
}

QJsonObject namedPayload(const QJsonObject &json,
                         std::initializer_list<const char *> keys) {
  const QJsonObject source = objectPayload(json);
  for (const char *key : keys) {
    const QJsonValue value = source.value(QString::fromLatin1(key));
    if (value.isObject()) {
      return value.toObject();
    }
  }
  return source;
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
               std::initializer_list<const char *> keys,
               bool fallback = false) {
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

qint64 firstLongLong(const QJsonObject &json,
                     std::initializer_list<const char *> keys,
                     qint64 fallback = 0) {
  for (const char *key : keys) {
    const QJsonValue value = json.value(QString::fromLatin1(key));
    if (value.isDouble()) {
      return static_cast<qint64>(value.toDouble());
    }
    if (value.isString()) {
      bool ok = false;
      const qint64 result = value.toString().toLongLong(&ok);
      if (ok) {
        return result;
      }
    }
  }
  return fallback;
}

double firstDouble(const QJsonObject &json,
                   std::initializer_list<const char *> keys,
                   double fallback = 0.0) {
  for (const char *key : keys) {
    const QJsonValue value = json.value(QString::fromLatin1(key));
    if (value.isDouble()) {
      return value.toDouble();
    }
    if (value.isString()) {
      bool ok = false;
      const double result = value.toString().toDouble(&ok);
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

QJsonObject firstObject(const QJsonObject &json,
                        std::initializer_list<const char *> keys) {
  for (const char *key : keys) {
    const QJsonValue value = json.value(QString::fromLatin1(key));
    if (value.isObject()) {
      return value.toObject();
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

QString responseRequestId(const QJsonObject &json, const QJsonObject &source) {
  const QString value = firstString(
      json, {"request_id", "requestId", "correlation_id", "correlationId"});
  return value.isEmpty()
             ? firstString(source, {"request_id", "requestId", "correlation_id",
                                    "correlationId"})
             : value;
}

QString responseNextCursor(const QJsonObject &json) {
  return firstString(json, {"next_cursor", "nextCursor", "cursor"});
}

bool pageHasMore(const QJsonObject &json, const QString &cursor) {
  const QJsonValue snakeCase = json.value(QStringLiteral("has_more"));
  if (!snakeCase.isUndefined()) {
    return snakeCase.toBool(!cursor.isEmpty());
  }
  const QJsonValue camelCase = json.value(QStringLiteral("hasMore"));
  return camelCase.isUndefined() ? !cursor.isEmpty()
                                 : camelCase.toBool(!cursor.isEmpty());
}

QJsonArray pageItems(const QJsonObject &json,
                     std::initializer_list<const char *> keys) {
  const QJsonArray values = firstArray(json, keys);
  if (!values.isEmpty()) {
    return values;
  }
  const QJsonValue data = json.value(QStringLiteral("data"));
  return data.isArray() ? data.toArray() : QJsonArray{};
}

QJsonObject actionPayload(const QJsonObject &json) {
  QJsonObject source = objectPayload(json);
  for (const char *key : {"result", "operation", "response"}) {
    const QJsonValue value = source.value(QString::fromLatin1(key));
    if (value.isObject()) {
      source = value.toObject();
      break;
    }
  }
  return source;
}

} // namespace

bool DashboardSummary::isValid() const { return !raw.isEmpty(); }

DashboardSummary DashboardSummary::fromJson(const QJsonObject &json) {
  const QJsonObject source = namedPayload(json, {"summary", "dashboard"});
  DashboardSummary result;
  result.status = firstString(source, {"status", "state"});
  result.generatedAt = firstTimestamp(
      source, {"generated_at", "generatedAt", "created_at", "timestamp"});
  result.counts = firstObject(source, {"counts", "totals"});
  result.metrics = firstObject(source, {"metrics", "statistics", "stats"});
  result.cameras = firstObject(source, {"cameras", "camera_summary"});
  result.members = firstObject(source, {"members", "member_summary"});
  result.notifications =
      firstObject(source, {"notifications", "notification_summary"});
  result.storage = firstObject(source, {"storage", "storage_summary"});
  result.system = firstObject(source, {"system", "system_summary"});
  result.requestId = responseRequestId(json, source);
  result.raw = source;
  return result;
}

bool DashboardActivityItem::isValid() const { return !raw.isEmpty(); }

DashboardActivityItem DashboardActivityItem::fromJson(const QJsonObject &json) {
  const QJsonObject source = namedPayload(json, {"activity", "event"});
  DashboardActivityItem result;
  result.id = firstString(
      source, {"id", "event_id", "eventId", "activity_id", "activityId"});
  result.type = firstString(source, {"type", "event_type", "eventType"});
  result.action = firstString(source, {"action", "operation", "verb"});
  result.title = firstString(source, {"title", "name"});
  result.message = firstString(source, {"message", "description"});
  result.actorId = firstString(source, {"actor_id", "actorId", "user_id",
                                        "userId", "member_id", "memberId"});
  result.actorName =
      firstString(source, {"actor_name", "actorName", "user_name", "userName"});
  result.entityType = firstString(
      source, {"entity_type", "entityType", "resource_type", "resourceType"});
  result.entityId = firstString(
      source, {"entity_id", "entityId", "resource_id", "resourceId"});
  result.occurredAt =
      firstTimestamp(source, {"occurred_at", "occurredAt", "created_at",
                              "createdAt", "timestamp"});
  result.metadata = firstObject(source, {"metadata", "meta", "details"});
  result.raw = source;
  return result;
}

bool DashboardActivity::isValid() const {
  if (raw.isEmpty()) {
    return false;
  }
  for (const DashboardActivityItem &item : items) {
    if (!item.isValid()) {
      return false;
    }
  }
  return true;
}

DashboardActivity DashboardActivity::fromJson(const QJsonObject &json) {
  const QJsonObject source =
      namedPayload(json, {"activity", "dashboard_activity"});
  const QJsonArray values =
      pageItems(source, {"items", "activity", "events", "results", "data"});
  DashboardActivity result;
  result.items.reserve(values.size());
  for (const QJsonValue &value : values) {
    if (value.isObject()) {
      result.items.append(DashboardActivityItem::fromJson(value.toObject()));
    }
  }
  result.nextCursor = responseNextCursor(source);
  result.hasMore = pageHasMore(source, result.nextCursor);
  result.raw = source;
  return result;
}

bool SystemHealth::isValid() const { return !raw.isEmpty(); }

SystemHealth SystemHealth::fromJson(const QJsonObject &json) {
  const QJsonObject source = namedPayload(json, {"health", "system_health"});
  SystemHealth result;
  result.status = firstString(source, {"status", "state"});
  result.version =
      firstString(source, {"version", "api_version", "apiVersion"});
  result.healthy =
      firstBool(source, {"healthy", "ok", "is_healthy", "isHealthy"},
                result.status.compare(QStringLiteral("healthy"),
                                      Qt::CaseInsensitive) == 0 ||
                    result.status.compare(QStringLiteral("ok"),
                                          Qt::CaseInsensitive) == 0);
  result.uptimeSeconds =
      firstDouble(source, {"uptime_seconds", "uptimeSeconds", "uptime"});
  result.checkedAt =
      firstTimestamp(source, {"checked_at", "checkedAt", "updated_at",
                              "updatedAt", "timestamp"});
  result.checks = firstObject(source, {"checks", "components"});
  result.services = firstObject(source, {"services", "dependencies"});
  result.details = firstObject(source, {"details", "metadata"});
  result.requestId = responseRequestId(json, source);
  result.raw = source;
  return result;
}

bool SystemSettings::isValid() const { return !raw.isEmpty(); }

SystemSettings SystemSettings::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  const QJsonObject values =
      firstObject(source, {"settings", "configuration", "system_settings",
                           "systemSettings"});
  SystemSettings result;
  result.values = values.isEmpty() ? source : values;
  result.requestId = responseRequestId(json, source);
  result.raw = source;
  return result;
}

SystemSettingsPatchResult
SystemSettingsPatchResult::fromJson(const QJsonObject &json) {
  const QJsonObject source = actionPayload(json);
  SystemSettingsPatchResult result;
  result.settings = SystemSettings::fromJson(objectPayload(json));
  result.status = firstString(source, {"status", "state", "result"});
  result.message = firstString(source, {"message", "description"});
  result.operationId = firstString(
      source, {"operation_id", "operationId", "job_id", "jobId", "id"});
  result.changes = firstObject(source, {"changes", "updated", "settings"});
  result.success = firstBool(source, {"success", "ok"}, false);
  result.accepted = firstBool(source, {"accepted", "queued"}, false);
  result.raw = source;
  return result;
}

StorageCleanupResult StorageCleanupResult::fromJson(const QJsonObject &json) {
  const QJsonObject source = actionPayload(json);
  StorageCleanupResult result;
  result.status = firstString(source, {"status", "state", "result"});
  result.message = firstString(source, {"message", "description"});
  result.operationId = firstString(
      source, {"operation_id", "operationId", "job_id", "jobId", "id"});
  result.deletedFiles =
      firstLongLong(source, {"deleted_files", "deletedFiles", "files_deleted",
                             "filesDeleted"});
  result.deletedBytes =
      firstLongLong(source, {"deleted_bytes", "deletedBytes", "bytes_deleted",
                             "bytesDeleted"});
  result.freedBytes = firstLongLong(
      source, {"freed_bytes", "freedBytes", "bytes_freed", "bytesFreed"});
  result.success = firstBool(source, {"success", "ok"}, false);
  result.accepted = firstBool(source, {"accepted", "queued"}, false);
  result.details = firstObject(source, {"details", "metadata"});
  result.raw = source;
  return result;
}

bool AuditEvent::isValid() const { return !raw.isEmpty(); }

AuditEvent AuditEvent::fromJson(const QJsonObject &json) {
  const QJsonObject source = namedPayload(json, {"event", "audit_event"});
  const QJsonObject actor = firstObject(source, {"actor", "user"});
  const QJsonObject resource = firstObject(source, {"resource", "entity"});
  AuditEvent result;
  result.id =
      firstString(source, {"id", "event_id", "eventId", "audit_id", "auditId"});
  result.action = firstString(source, {"action", "operation", "verb"});
  result.category = firstString(source, {"category", "domain"});
  result.type = firstString(source, {"type", "event_type", "eventType"});
  result.actorId =
      firstString(source, {"actor_id", "actorId", "user_id", "userId"});
  if (result.actorId.isEmpty()) {
    result.actorId = firstString(actor, {"id", "user_id", "userId"});
  }
  result.actorName =
      firstString(source, {"actor_name", "actorName", "user_name", "userName"});
  if (result.actorName.isEmpty()) {
    result.actorName =
        firstString(actor, {"name", "username", "user_name", "userName"});
  }
  result.actorType = firstString(source, {"actor_type", "actorType"});
  result.resourceType = firstString(
      source, {"resource_type", "resourceType", "entity_type", "entityType"});
  if (result.resourceType.isEmpty()) {
    result.resourceType =
        firstString(resource, {"type", "resource_type", "resourceType"});
  }
  result.resourceId = firstString(
      source, {"resource_id", "resourceId", "entity_id", "entityId"});
  if (result.resourceId.isEmpty()) {
    result.resourceId =
        firstString(resource, {"id", "resource_id", "resourceId"});
  }
  result.ipAddress = firstString(source, {"ip_address", "ipAddress", "ip"});
  result.userAgent = firstString(source, {"user_agent", "userAgent"});
  result.occurredAt =
      firstTimestamp(source, {"occurred_at", "occurredAt", "created_at",
                              "createdAt", "timestamp"});
  result.details = firstObject(source, {"details", "data"});
  result.metadata = firstObject(source, {"metadata", "meta"});
  result.raw = source;
  return result;
}

bool AuditEventPage::isValid() const {
  if (raw.isEmpty()) {
    return false;
  }
  for (const AuditEvent &item : items) {
    if (!item.isValid()) {
      return false;
    }
  }
  return true;
}

AuditEventPage AuditEventPage::fromJson(const QJsonObject &json) {
  const QJsonObject source =
      namedPayload(json, {"events", "audit_events", "auditEvents", "page"});
  const QJsonArray values =
      pageItems(source, {"items", "events", "audit_events", "auditEvents",
                         "results", "data"});
  AuditEventPage result;
  result.items.reserve(values.size());
  for (const QJsonValue &value : values) {
    if (value.isObject()) {
      result.items.append(AuditEvent::fromJson(value.toObject()));
    }
  }
  result.nextCursor = responseNextCursor(source);
  result.hasMore = pageHasMore(source, result.nextCursor);
  result.raw = source;
  return result;
}

bool NvrStatus::isValid() const { return !raw.isEmpty(); }

NvrStatus NvrStatus::fromJson(const QJsonObject &json) {
  const QJsonObject source =
      namedPayload(json, {"nvr", "nvr_status", "nvrStatus"});
  NvrStatus result;
  result.status = firstString(source, {"status", "health"});
  result.state =
      firstString(source, {"state", "connection_state", "connectionState"});
  result.host = firstString(source, {"host", "hostname", "address"});
  result.version =
      firstString(source, {"version", "firmware_version", "firmwareVersion"});
  result.online = firstBool(source, {"online", "is_online", "isOnline"});
  result.connected = firstBool(
      source, {"connected", "is_connected", "isConnected"}, result.online);
  result.cameraCount =
      firstInt(source, {"camera_count", "cameraCount", "cameras", "channels"});
  result.streamCount =
      firstInt(source, {"stream_count", "streamCount", "streams"});
  result.updatedAt =
      firstTimestamp(source, {"updated_at", "updatedAt", "checked_at",
                              "checkedAt", "timestamp"});
  result.details = firstObject(source, {"details", "metadata"});
  result.raw = source;
  return result;
}

bool PoolStatus::isValid() const { return !raw.isEmpty(); }

PoolStatus PoolStatus::fromJson(const QJsonObject &json) {
  const QJsonObject source =
      namedPayload(json, {"pool", "pool_status", "poolStatus"});
  PoolStatus result;
  result.status = firstString(source, {"status", "health"});
  result.state = firstString(source, {"state"});
  result.healthy =
      firstBool(source, {"healthy", "ok", "is_healthy", "isHealthy"},
                result.status.compare(QStringLiteral("healthy"),
                                      Qt::CaseInsensitive) == 0 ||
                    result.status.compare(QStringLiteral("ok"),
                                          Qt::CaseInsensitive) == 0);
  result.totalNodes = firstInt(
      source, {"total_nodes", "totalNodes", "node_count", "nodeCount"});
  result.onlineNodes = firstInt(
      source, {"online_nodes", "onlineNodes", "active_nodes", "activeNodes"});
  result.offlineNodes = firstInt(source, {"offline_nodes", "offlineNodes"});
  result.updatedAt =
      firstTimestamp(source, {"updated_at", "updatedAt", "checked_at",
                              "checkedAt", "timestamp"});
  result.nodes = firstArray(source, {"nodes", "members", "pool_nodes"});
  result.details = firstObject(source, {"details", "metadata"});
  result.raw = source;
  return result;
}

PoolSyncResult PoolSyncResult::fromJson(const QJsonObject &json) {
  const QJsonObject source = actionPayload(json);
  PoolSyncResult result;
  result.status = firstString(source, {"status", "state", "result"});
  result.message = firstString(source, {"message", "description"});
  result.operationId = firstString(
      source, {"operation_id", "operationId", "job_id", "jobId", "id"});
  result.synchronizedCount =
      firstInt(source, {"synchronized", "synchronized_count",
                        "synchronizedCount", "synced", "count"});
  result.success = firstBool(source, {"success", "ok"}, false);
  result.accepted = firstBool(source, {"accepted", "queued"}, false);
  result.details = firstObject(source, {"details", "metadata"});
  result.raw = source;
  return result;
}

} // namespace HubSight::Admin
