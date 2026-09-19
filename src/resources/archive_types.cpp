#include "../../include/hubsight/admin/resources/archive_types.h"

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

qint64 integerValue(const QJsonObject &json,
                    std::initializer_list<const char *> keys) {
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
  return 0;
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

bool RecordingSegment::isValid() const { return !id.trimmed().isEmpty(); }

RecordingSegment RecordingSegment::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  RecordingSegment result;
  result.id = firstString(source, {"id", "recording_id"});
  result.cameraId = firstString(source, {"camera_id", "cameraId"});
  result.startAt = firstTimestamp(source, {"start_at", "startAt", "start"});
  result.endAt = firstTimestamp(source, {"end_at", "endAt", "end"});
  result.durationSeconds = static_cast<int>(integerValue(
      source, {"duration_seconds", "durationSeconds", "duration"}));
  result.sizeBytes = integerValue(source, {"size_bytes", "sizeBytes", "size"});
  result.createdAt = firstTimestamp(source, {"created_at", "createdAt"});
  result.raw = source;
  return result;
}

bool ArchiveTimelinePage::isValid() const {
  for (const RecordingSegment &item : items) {
    if (!item.isValid()) {
      return false;
    }
  }
  return !raw.isEmpty();
}

ArchiveTimelinePage ArchiveTimelinePage::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  ArchiveTimelinePage result;
  const QJsonArray values =
      arrayValue(source, {"items", "recordings", "segments", "data"});
  result.items.reserve(values.size());
  for (const QJsonValue &value : values) {
    if (value.isObject()) {
      result.items.append(RecordingSegment::fromJson(value.toObject()));
    }
  }
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

bool ArchiveAvailableDays::isValid() const {
  if (cameraId.trimmed().isEmpty() || year < 1 || month < 1 || month > 12) {
    return false;
  }
  for (const int day : days) {
    if (day < 1 || day > 31) {
      return false;
    }
  }
  return !raw.isEmpty();
}

ArchiveAvailableDays ArchiveAvailableDays::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  ArchiveAvailableDays result;
  result.cameraId = firstString(source, {"camera_id", "cameraId"});
  result.year = static_cast<int>(integerValue(source, {"year"}));
  result.month = static_cast<int>(integerValue(source, {"month"}));

  QJsonArray values = arrayValue(source, {"days", "available_days", "dates"});
  if (values.isEmpty() && json.value(QStringLiteral("data")).isArray()) {
    values = json.value(QStringLiteral("data")).toArray();
  }
  for (const QJsonValue &value : values) {
    if (value.isDouble()) {
      result.days.append(static_cast<int>(value.toDouble()));
    } else if (value.isString()) {
      bool ok = false;
      const int day = value.toString().toInt(&ok);
      if (ok) {
        result.days.append(day);
      }
    }
  }
  result.raw = source;
  return result;
}

bool ArchiveUrlResult::isValid() const {
  return !recordingId.trimmed().isEmpty() && !url.trimmed().isEmpty();
}

ArchiveUrlResult ArchiveUrlResult::fromJson(const QJsonObject &json,
                                            const QString &recordingId) {
  const QJsonValue data = json.value(QStringLiteral("data"));
  const QJsonObject source = data.isObject() ? data.toObject() : json;
  ArchiveUrlResult result;
  result.recordingId = recordingId.trimmed();
  if (result.recordingId.isEmpty()) {
    result.recordingId =
        firstString(source, {"recording_id", "recordingId", "id"});
  }
  if (data.isString()) {
    result.url = data.toString();
  } else {
    result.url = firstString(source, {"url", "playback_url", "download_url",
                                      "thumbnail_url", "stream_url",
                                      "presigned_url", "location"});
  }
  result.expiresAt =
      firstTimestamp(source, {"expires_at", "expiresAt", "expiry", "expires"});
  result.raw = source;
  return result;
}

} // namespace HubSight::Admin
