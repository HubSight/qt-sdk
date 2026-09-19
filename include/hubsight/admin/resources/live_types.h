#pragma once

#include "../admin_export.h"
#include "../webrtc/webrtc_types.h"

#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace HubSight::Admin {

struct HUBSIGHT_ADMIN_EXPORT LiveCapabilities {
  int maxConcurrentSessions = 0;
  QStringList supportedProfiles;
  QStringList supportedTransports;
  QStringList supportedCodecs;
  QString signalingTransport;
  QString mediaTransport;
  bool webRtcSupported = false;
  bool trickleIceSupported = true;
  WebRtcConfiguration webRtcConfiguration;
  QString requestId;
  QJsonObject raw;

  bool isValid() const;
  static LiveCapabilities fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT LiveCameraSummary {
  QString id;
  QString name;
  bool online = false;
  bool available = false;
  QStringList supportedProfiles;
  QJsonObject raw;

  bool isValid() const;
  static LiveCameraSummary fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT LiveCameraPage {
  QVector<LiveCameraSummary> items;
  QString nextCursor;
  bool hasMore = false;
};

struct HUBSIGHT_ADMIN_EXPORT LiveSession {
  QString sessionId;
  QString cameraId;
  QString profile;
  QString state;
  QString mediaTransport;
  QDateTime expiresAt;
  WebRtcConfiguration webRtcConfiguration;
  WebRtcSessionDescription remoteDescription;
  bool hasRemoteDescription = false;
  QVector<WebRtcIceCandidate> remoteCandidates;
  QJsonObject signaling;
  QJsonObject raw;

  bool isValid() const;
  static LiveSession fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT LiveSessionStats {
  QString sessionId;
  QDateTime collectedAt;
  QJsonObject values;
  QJsonObject raw;

  bool isValid() const;
  static LiveSessionStats fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT LiveCameraStatus {
  QString cameraId;
  QString status;
  bool online = false;
  bool available = false;
  QJsonObject details;
  QJsonObject raw;

  bool isValid() const;
  static LiveCameraStatus fromJson(const QJsonObject &json);
};

} // namespace HubSight::Admin

Q_DECLARE_METATYPE(HubSight::Admin::LiveCapabilities)
Q_DECLARE_METATYPE(HubSight::Admin::LiveCameraSummary)
Q_DECLARE_METATYPE(HubSight::Admin::LiveCameraPage)
Q_DECLARE_METATYPE(HubSight::Admin::LiveSession)
Q_DECLARE_METATYPE(HubSight::Admin::LiveSessionStats)
Q_DECLARE_METATYPE(HubSight::Admin::LiveCameraStatus)
