#pragma once

#include "../admin_types.h"

#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace HubSight::Admin {

HUBSIGHT_ADMIN_EXPORT QString toString(WebRtcSdpType type);
HUBSIGHT_ADMIN_EXPORT QString toString(WebRtcPeerConnectionState state);
HUBSIGHT_ADMIN_EXPORT QString toString(WebRtcIceConnectionState state);
HUBSIGHT_ADMIN_EXPORT QString toString(WebRtcIceGatheringState state);
HUBSIGHT_ADMIN_EXPORT QString toString(WebRtcSignalingState state);
HUBSIGHT_ADMIN_EXPORT QString toString(WebRtcMediaKind kind);
HUBSIGHT_ADMIN_EXPORT QString toString(WebRtcIceTransportPolicy policy);
HUBSIGHT_ADMIN_EXPORT QString toString(WebRtcBundlePolicy policy);
HUBSIGHT_ADMIN_EXPORT QString toString(WebRtcErrorCode code);

HUBSIGHT_ADMIN_EXPORT WebRtcSdpType
webRtcSdpTypeFromString(const QString &value);
HUBSIGHT_ADMIN_EXPORT WebRtcIceTransportPolicy
webRtcIceTransportPolicyFromString(const QString &value);
HUBSIGHT_ADMIN_EXPORT WebRtcBundlePolicy
webRtcBundlePolicyFromString(const QString &value);

struct HUBSIGHT_ADMIN_EXPORT WebRtcError {
  WebRtcErrorCode code = WebRtcErrorCode::Unknown;
  QString message;
  QString operation;
  bool retryable = false;
};

// ICE server URLs intentionally use the WebRTC URI schemes only. Credentials
// belong in the dedicated fields below and are never accepted from URL
// user-info; only the standard transport=udp/tcp query is permitted.
struct HUBSIGHT_ADMIN_EXPORT WebRtcIceServer {
  QStringList urls;
  QString username;
  QString credential;

  bool isValid(QString *reason = nullptr) const;
  QJsonObject toJson() const;
  static WebRtcIceServer fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT WebRtcConfiguration {
  QVector<WebRtcIceServer> iceServers;
  WebRtcIceTransportPolicy iceTransportPolicy = WebRtcIceTransportPolicy::All;
  WebRtcBundlePolicy bundlePolicy = WebRtcBundlePolicy::Balanced;
  int iceCandidatePoolSize = 0;

  bool isValid(QString *reason = nullptr) const;
  QJsonObject toJson() const;
  static WebRtcConfiguration fromJson(const QJsonObject &json);
};

// JSON uses the standard WebRTC signaling shape: {"type": ..., "sdp": ...}.
struct HUBSIGHT_ADMIN_EXPORT WebRtcSessionDescription {
  WebRtcSdpType type = WebRtcSdpType::Offer;
  QString sdp;

  bool isValid(QString *reason = nullptr) const;
  QJsonObject toJson() const;
  static WebRtcSessionDescription fromJson(const QJsonObject &json);
};

// An empty candidate represents the end-of-candidates marker. It is valid to
// forward that marker to a backend during trickle ICE completion.
struct HUBSIGHT_ADMIN_EXPORT WebRtcIceCandidate {
  QString candidate;
  QString sdpMid;
  int sdpMLineIndex = -1;
  QString usernameFragment;

  bool isEndOfCandidates() const;
  bool isValid(QString *reason = nullptr) const;
  QJsonObject toJson() const;
  static WebRtcIceCandidate fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT WebRtcTrackInfo {
  WebRtcMediaKind kind = WebRtcMediaKind::Video;
  QString id;
  QString label;
  QStringList streamIds;
  bool enabled = true;

  QJsonObject toJson() const;
  static WebRtcTrackInfo fromJson(const QJsonObject &json);
};

} // namespace HubSight::Admin

Q_DECLARE_METATYPE(HubSight::Admin::WebRtcSdpType)
Q_DECLARE_METATYPE(HubSight::Admin::WebRtcPeerConnectionState)
Q_DECLARE_METATYPE(HubSight::Admin::WebRtcIceConnectionState)
Q_DECLARE_METATYPE(HubSight::Admin::WebRtcIceGatheringState)
Q_DECLARE_METATYPE(HubSight::Admin::WebRtcSignalingState)
Q_DECLARE_METATYPE(HubSight::Admin::WebRtcMediaKind)
Q_DECLARE_METATYPE(HubSight::Admin::WebRtcIceTransportPolicy)
Q_DECLARE_METATYPE(HubSight::Admin::WebRtcBundlePolicy)
Q_DECLARE_METATYPE(HubSight::Admin::WebRtcErrorCode)
Q_DECLARE_METATYPE(HubSight::Admin::WebRtcError)
Q_DECLARE_METATYPE(HubSight::Admin::WebRtcIceServer)
Q_DECLARE_METATYPE(HubSight::Admin::WebRtcConfiguration)
Q_DECLARE_METATYPE(HubSight::Admin::WebRtcSessionDescription)
Q_DECLARE_METATYPE(HubSight::Admin::WebRtcIceCandidate)
Q_DECLARE_METATYPE(HubSight::Admin::WebRtcTrackInfo)
