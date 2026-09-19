#include "../../include/hubsight/admin/resources/camera_management_types.h"

#include <initializer_list>

namespace HubSight::Admin {
namespace {

QJsonObject objectPayload(const QJsonObject &json) {
  const QJsonValue data = json.value(QStringLiteral("data"));
  return data.isObject() ? data.toObject() : json;
}

QJsonObject singletonPayload(const QJsonObject &json,
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

double firstDouble(const QJsonObject &json,
                   std::initializer_list<const char *> keys) {
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
  return 0.0;
}

QString pageNextCursor(const QJsonObject &json) {
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
  QJsonArray result = firstArray(json, keys);
  if (!result.isEmpty()) {
    return result;
  }
  // When the API uses {"data": [...]} there is no object payload to unwrap.
  const QJsonValue data = json.value(QStringLiteral("data"));
  return data.isArray() ? data.toArray() : result;
}

} // namespace

CameraActionResult CameraActionResult::fromJson(const QJsonObject &json,
                                                const QString &cameraId,
                                                const QString &jobId) {
  const QJsonObject source = objectPayload(json);
  CameraActionResult result;
  result.cameraId = cameraId.trimmed().isEmpty()
                        ? firstString(source, {"camera_id", "cameraId"})
                        : cameraId.trimmed();
  result.jobId = jobId.trimmed().isEmpty()
                     ? firstString(source, {"job_id", "jobId", "id"})
                     : jobId.trimmed();
  result.status = firstString(source, {"status", "result", "state"});
  result.accepted = firstBool(source, {"accepted", "success", "ok"}, false);
  result.affectedCount =
      firstInt(source, {"affected", "affected_count", "affectedCount",
                        "deleted", "count"});
  result.raw = source;
  return result;
}

bool CameraMedia::isValid() const {
  return !cameraId.trimmed().isEmpty() &&
         (!bytes.isEmpty() || !url.trimmed().isEmpty() || !raw.isEmpty());
}

CameraMedia CameraMedia::fromJson(const QJsonObject &json,
                                  const QString &cameraId) {
  const QJsonObject source = objectPayload(json);
  CameraMedia result;
  result.cameraId = cameraId.trimmed().isEmpty()
                        ? firstString(source, {"camera_id", "cameraId"})
                        : cameraId.trimmed();
  result.url =
      firstString(source, {"url", "media_url", "image_url", "thumbnail_url",
                           "snapshot_url", "presigned_url", "location"});
  if (result.url.isEmpty() && source.value(QStringLiteral("data")).isString()) {
    result.url = source.value(QStringLiteral("data")).toString();
  }
  result.mimeType = firstString(
      source, {"mime_type", "mimeType", "content_type", "contentType", "type"});
  result.metadata = firstObject(source, {"metadata", "meta"});
  result.raw = source;
  return result;
}

bool CameraHomography::isValid() const { return !cameraId.trimmed().isEmpty(); }

CameraHomography CameraHomography::fromJson(const QJsonObject &json,
                                            const QString &cameraId) {
  const QJsonObject source =
      singletonPayload(json, {"homography", "transform"});
  CameraHomography result;
  result.cameraId = cameraId.trimmed().isEmpty()
                        ? firstString(source, {"camera_id", "cameraId"})
                        : cameraId.trimmed();
  result.matrix = firstArray(source, {"matrix", "homography"});
  result.points = firstObject(source, {"points", "corners", "coordinates"});
  if (source.contains(QStringLiteral("transform"))) {
    result.transform = source.value(QStringLiteral("transform"));
  } else if (source.contains(QStringLiteral("homography"))) {
    result.transform = source.value(QStringLiteral("homography"));
  }
  result.valid =
      firstBool(source, {"valid", "is_valid", "homography_valid"},
                !result.matrix.isEmpty() || !result.points.isEmpty() ||
                    !result.transform.isUndefined());
  result.raw = source;
  return result;
}

bool CameraPreset::isValid() const { return !id.trimmed().isEmpty(); }

CameraPreset CameraPreset::fromJson(const QJsonObject &json,
                                    const QString &cameraId) {
  const QJsonObject source = singletonPayload(json, {"preset"});
  CameraPreset result;
  result.id = firstString(source, {"id", "preset_id", "presetId"});
  result.cameraId = cameraId.trimmed().isEmpty()
                        ? firstString(source, {"camera_id", "cameraId"})
                        : cameraId.trimmed();
  result.name = firstString(source, {"name", "label"});
  result.position = firstObject(source, {"position", "coordinates"});
  result.settings = firstObject(source, {"settings", "configuration"});
  result.raw = source;
  return result;
}

bool CameraPresetPage::isValid() const {
  if (raw.isEmpty()) {
    return false;
  }
  for (const CameraPreset &item : items) {
    if (!item.isValid()) {
      return false;
    }
  }
  return true;
}

CameraPresetPage CameraPresetPage::fromJson(const QJsonObject &json,
                                            const QString &cameraId) {
  const QJsonObject source = objectPayload(json);
  CameraPresetPage result;
  const QJsonArray values =
      pageItems(source, {"presets", "items", "results", "data"});
  result.items.reserve(values.size());
  for (const QJsonValue &value : values) {
    if (value.isObject()) {
      result.items.append(CameraPreset::fromJson(value.toObject(), cameraId));
    }
  }
  result.nextCursor = pageNextCursor(source);
  result.hasMore = pageHasMore(source, result.nextCursor);
  result.raw = source;
  return result;
}

bool CameraDiscoveryJob::isValid() const { return !jobId.trimmed().isEmpty(); }

CameraDiscoveryJob CameraDiscoveryJob::fromJson(const QJsonObject &json,
                                                const QString &jobId) {
  const QJsonObject source =
      singletonPayload(json, {"job", "discovery_job", "scan"});
  CameraDiscoveryJob result;
  result.jobId = jobId.trimmed().isEmpty()
                     ? firstString(source, {"job_id", "jobId", "id"})
                     : jobId.trimmed();
  result.status = firstString(source, {"status", "state"});
  result.progress =
      firstInt(source, {"progress", "progress_percent", "percent_complete"});
  result.cameras = firstArray(source, {"cameras", "results", "devices"});
  result.raw = source;
  return result;
}

bool CameraOnvifProbeResult::isValid() const { return !raw.isEmpty(); }

CameraOnvifProbeResult
CameraOnvifProbeResult::fromJson(const QJsonObject &json,
                                 const QString &cameraId) {
  const QJsonObject source = singletonPayload(json, {"probe", "onvif"});
  CameraOnvifProbeResult result;
  result.cameraId = cameraId.trimmed().isEmpty()
                        ? firstString(source, {"camera_id", "cameraId"})
                        : cameraId.trimmed();
  result.host = firstString(source, {"host", "address", "ip"});
  result.brand = firstString(source, {"brand", "manufacturer", "make"});
  result.port = firstInt(source, {"port", "onvif_port"});
  result.reachable = firstBool(source, {"reachable", "online"}, false);
  result.onvifEnabled =
      firstBool(source, {"onvif_enabled", "onvifEnabled", "supported"}, false);
  result.ptzSupported = firstBool(
      source, {"ptz_supported", "ptzSupported", "onvif_ptz_supported"}, false);
  result.deviceInfo = firstObject(source, {"device_info", "deviceInfo"});
  result.raw = source;
  return result;
}

bool CameraRecognitionLog::isValid() const { return !id.trimmed().isEmpty(); }

CameraRecognitionLog CameraRecognitionLog::fromJson(const QJsonObject &json,
                                                    const QString &cameraId) {
  const QJsonObject source = singletonPayload(json, {"log", "recognition_log"});
  CameraRecognitionLog result;
  result.id = firstString(source, {"id", "log_id", "recognition_log_id"});
  result.cameraId = cameraId.trimmed().isEmpty()
                        ? firstString(source, {"camera_id", "cameraId"})
                        : cameraId.trimmed();
  result.memberId = firstString(source, {"member_id", "memberId"});
  result.eventType = firstString(source, {"event_type", "eventType", "type"});
  result.confidence = firstDouble(source, {"confidence", "score"});
  result.occurredAt = firstTimestamp(
      source, {"occurred_at", "occurredAt", "created_at", "timestamp"});
  result.imageUrl = firstString(
      source, {"image_url", "imageUrl", "thumbnail_url", "thumbnailUrl"});
  result.details = firstObject(source, {"details", "metadata"});
  result.raw = source;
  return result;
}

bool CameraRecognitionLogPage::isValid() const {
  if (raw.isEmpty()) {
    return false;
  }
  for (const CameraRecognitionLog &item : items) {
    if (!item.isValid()) {
      return false;
    }
  }
  return true;
}

CameraRecognitionLogPage
CameraRecognitionLogPage::fromJson(const QJsonObject &json,
                                   const QString &cameraId) {
  const QJsonObject source = objectPayload(json);
  CameraRecognitionLogPage result;
  const QJsonArray values = pageItems(
      source, {"logs", "recognition_logs", "items", "results", "data"});
  result.items.reserve(values.size());
  for (const QJsonValue &value : values) {
    if (value.isObject()) {
      result.items.append(
          CameraRecognitionLog::fromJson(value.toObject(), cameraId));
    }
  }
  result.nextCursor = pageNextCursor(source);
  result.hasMore = pageHasMore(source, result.nextCursor);
  result.raw = source;
  return result;
}

} // namespace HubSight::Admin
