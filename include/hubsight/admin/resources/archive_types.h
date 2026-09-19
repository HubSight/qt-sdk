#pragma once

#include "../admin_export.h"

#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QVector>

namespace HubSight::Admin {

struct HUBSIGHT_ADMIN_EXPORT RecordingSegment {
  QString id;
  QString cameraId;
  QDateTime startAt;
  QDateTime endAt;
  int durationSeconds = 0;
  qint64 sizeBytes = 0;
  QDateTime createdAt;
  QJsonObject raw;

  bool isValid() const;
  static RecordingSegment fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT ArchiveTimelinePage {
  QVector<RecordingSegment> items;
  QString nextCursor;
  bool hasMore = false;
  QJsonObject raw;

  bool isValid() const;
  static ArchiveTimelinePage fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT ArchiveAvailableDays {
  QString cameraId;
  int year = 0;
  int month = 0;
  QVector<int> days;
  QJsonObject raw;

  bool isValid() const;
  static ArchiveAvailableDays fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT ArchiveUrlResult {
  QString recordingId;
  QString url;
  QDateTime expiresAt;
  QJsonObject raw;

  bool isValid() const;
  static ArchiveUrlResult fromJson(const QJsonObject &json,
                                   const QString &recordingId = {});
};

} // namespace HubSight::Admin

Q_DECLARE_METATYPE(HubSight::Admin::RecordingSegment)
Q_DECLARE_METATYPE(HubSight::Admin::ArchiveTimelinePage)
Q_DECLARE_METATYPE(HubSight::Admin::ArchiveAvailableDays)
Q_DECLARE_METATYPE(HubSight::Admin::ArchiveUrlResult)
