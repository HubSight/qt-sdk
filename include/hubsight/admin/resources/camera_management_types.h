#pragma once

#include "../admin_export.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaType>
#include <QString>
#include <QVector>

namespace HubSight::Admin {

// Common response for camera actions whose server response is not a complete
// camera resource (for example start, stop, restart, and delete).
struct HUBSIGHT_ADMIN_EXPORT CameraActionResult {
  QString cameraId;
  QString jobId;
  QString status;
  bool accepted = false;
  int affectedCount = 0;
  QJsonObject raw;

  static CameraActionResult fromJson(const QJsonObject &json,
                                     const QString &cameraId = {},
                                     const QString &jobId = {});
};

// Thumbnail and snapshot endpoints may return either an image body or a JSON
// envelope containing a URL/metadata object. The client preserves both forms.
struct HUBSIGHT_ADMIN_EXPORT CameraMedia {
  QString cameraId;
  QString url;
  QString mimeType;
  QByteArray bytes;
  QJsonObject metadata;
  QJsonObject raw;

  bool isValid() const;
  static CameraMedia fromJson(const QJsonObject &json,
                              const QString &cameraId = {});
};

struct HUBSIGHT_ADMIN_EXPORT CameraHomography {
  QString cameraId;
  QJsonArray matrix;
  QJsonObject points;
  QJsonValue transform;
  bool valid = false;
  QJsonObject raw;

  bool isValid() const;
  static CameraHomography fromJson(const QJsonObject &json,
                                   const QString &cameraId = {});
};

struct HUBSIGHT_ADMIN_EXPORT CameraPreset {
  QString id;
  QString cameraId;
  QString name;
  QJsonObject position;
  QJsonObject settings;
  QJsonObject raw;

  bool isValid() const;
  static CameraPreset fromJson(const QJsonObject &json,
                               const QString &cameraId = {});
};

struct HUBSIGHT_ADMIN_EXPORT CameraPresetPage {
  QVector<CameraPreset> items;
  QString nextCursor;
  bool hasMore = false;
  QJsonObject raw;

  bool isValid() const;
  static CameraPresetPage fromJson(const QJsonObject &json,
                                   const QString &cameraId = {});
};

struct HUBSIGHT_ADMIN_EXPORT CameraDiscoveryJob {
  QString jobId;
  QString status;
  int progress = 0;
  QJsonArray cameras;
  QJsonObject raw;

  bool isValid() const;
  static CameraDiscoveryJob fromJson(const QJsonObject &json,
                                     const QString &jobId = {});
};

struct HUBSIGHT_ADMIN_EXPORT CameraOnvifProbeResult {
  QString cameraId;
  QString host;
  QString brand;
  int port = 0;
  bool reachable = false;
  bool onvifEnabled = false;
  bool ptzSupported = false;
  QJsonObject deviceInfo;
  QJsonObject raw;

  bool isValid() const;
  static CameraOnvifProbeResult fromJson(const QJsonObject &json,
                                         const QString &cameraId = {});
};

struct HUBSIGHT_ADMIN_EXPORT CameraRecognitionLog {
  QString id;
  QString cameraId;
  QString memberId;
  QString eventType;
  double confidence = 0.0;
  QDateTime occurredAt;
  QString imageUrl;
  QJsonObject details;
  QJsonObject raw;

  bool isValid() const;
  static CameraRecognitionLog fromJson(const QJsonObject &json,
                                       const QString &cameraId = {});
};

struct HUBSIGHT_ADMIN_EXPORT CameraRecognitionLogPage {
  QVector<CameraRecognitionLog> items;
  QString nextCursor;
  bool hasMore = false;
  QJsonObject raw;

  bool isValid() const;
  static CameraRecognitionLogPage fromJson(const QJsonObject &json,
                                           const QString &cameraId = {});
};

} // namespace HubSight::Admin

Q_DECLARE_METATYPE(HubSight::Admin::CameraActionResult)
Q_DECLARE_METATYPE(HubSight::Admin::CameraMedia)
Q_DECLARE_METATYPE(HubSight::Admin::CameraHomography)
Q_DECLARE_METATYPE(HubSight::Admin::CameraPreset)
Q_DECLARE_METATYPE(HubSight::Admin::CameraPresetPage)
Q_DECLARE_METATYPE(HubSight::Admin::CameraDiscoveryJob)
Q_DECLARE_METATYPE(HubSight::Admin::CameraOnvifProbeResult)
Q_DECLARE_METATYPE(HubSight::Admin::CameraRecognitionLog)
Q_DECLARE_METATYPE(HubSight::Admin::CameraRecognitionLogPage)
