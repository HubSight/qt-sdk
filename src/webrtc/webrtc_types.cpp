#include "../../include/hubsight/admin/webrtc/webrtc_types.h"

#include <QJsonArray>
#include <QList>
#include <QPair>
#include <QUrl>
#include <QUrlQuery>

namespace HubSight::Admin {
namespace {

QString normalized(const QString &value) { return value.trimmed().toLower(); }

bool fail(QString *reason, const QString &message) {
  if (reason) {
    *reason = message;
  }
  return false;
}

bool isAllowedIceQuery(const QUrl &url) {
  const QUrlQuery query(url);
  const QList<QPair<QString, QString>> items = query.queryItems();
  for (const auto &item : items) {
    if (item.first.compare(QStringLiteral("transport"), Qt::CaseInsensitive) !=
            0 ||
        (item.second.compare(QStringLiteral("udp"), Qt::CaseInsensitive) != 0 &&
         item.second.compare(QStringLiteral("tcp"), Qt::CaseInsensitive) !=
             0)) {
      return false;
    }
  }
  return true;
}

WebRtcMediaKind mediaKindFromString(const QString &value) {
  const QString kind = normalized(value);
  if (kind == QStringLiteral("audio")) {
    return WebRtcMediaKind::Audio;
  }
  if (kind == QStringLiteral("data")) {
    return WebRtcMediaKind::Data;
  }
  return WebRtcMediaKind::Video;
}

} // namespace

QString toString(WebRtcSdpType type) {
  switch (type) {
  case WebRtcSdpType::Offer:
    return QStringLiteral("offer");
  case WebRtcSdpType::Pranswer:
    return QStringLiteral("pranswer");
  case WebRtcSdpType::Answer:
    return QStringLiteral("answer");
  case WebRtcSdpType::Rollback:
    return QStringLiteral("rollback");
  case WebRtcSdpType::Invalid:
    return QStringLiteral("invalid");
  }
  return QStringLiteral("invalid");
}

QString toString(WebRtcPeerConnectionState state) {
  switch (state) {
  case WebRtcPeerConnectionState::New:
    return QStringLiteral("new");
  case WebRtcPeerConnectionState::Connecting:
    return QStringLiteral("connecting");
  case WebRtcPeerConnectionState::Connected:
    return QStringLiteral("connected");
  case WebRtcPeerConnectionState::Disconnected:
    return QStringLiteral("disconnected");
  case WebRtcPeerConnectionState::Failed:
    return QStringLiteral("failed");
  case WebRtcPeerConnectionState::Closed:
    return QStringLiteral("closed");
  }
  return QStringLiteral("new");
}

QString toString(WebRtcIceConnectionState state) {
  switch (state) {
  case WebRtcIceConnectionState::New:
    return QStringLiteral("new");
  case WebRtcIceConnectionState::Checking:
    return QStringLiteral("checking");
  case WebRtcIceConnectionState::Connected:
    return QStringLiteral("connected");
  case WebRtcIceConnectionState::Completed:
    return QStringLiteral("completed");
  case WebRtcIceConnectionState::Disconnected:
    return QStringLiteral("disconnected");
  case WebRtcIceConnectionState::Failed:
    return QStringLiteral("failed");
  case WebRtcIceConnectionState::Closed:
    return QStringLiteral("closed");
  }
  return QStringLiteral("new");
}

QString toString(WebRtcIceGatheringState state) {
  switch (state) {
  case WebRtcIceGatheringState::New:
    return QStringLiteral("new");
  case WebRtcIceGatheringState::Gathering:
    return QStringLiteral("gathering");
  case WebRtcIceGatheringState::Complete:
    return QStringLiteral("complete");
  }
  return QStringLiteral("new");
}

QString toString(WebRtcSignalingState state) {
  switch (state) {
  case WebRtcSignalingState::Stable:
    return QStringLiteral("stable");
  case WebRtcSignalingState::HaveLocalOffer:
    return QStringLiteral("have-local-offer");
  case WebRtcSignalingState::HaveRemoteOffer:
    return QStringLiteral("have-remote-offer");
  case WebRtcSignalingState::Closed:
    return QStringLiteral("closed");
  }
  return QStringLiteral("stable");
}

QString toString(WebRtcMediaKind kind) {
  switch (kind) {
  case WebRtcMediaKind::Audio:
    return QStringLiteral("audio");
  case WebRtcMediaKind::Video:
    return QStringLiteral("video");
  case WebRtcMediaKind::Data:
    return QStringLiteral("data");
  }
  return QStringLiteral("video");
}

QString toString(WebRtcIceTransportPolicy policy) {
  switch (policy) {
  case WebRtcIceTransportPolicy::All:
    return QStringLiteral("all");
  case WebRtcIceTransportPolicy::Relay:
    return QStringLiteral("relay");
  }
  return QStringLiteral("all");
}

QString toString(WebRtcBundlePolicy policy) {
  switch (policy) {
  case WebRtcBundlePolicy::Balanced:
    return QStringLiteral("balanced");
  case WebRtcBundlePolicy::MaxBundle:
    return QStringLiteral("max-bundle");
  case WebRtcBundlePolicy::MaxCompat:
    return QStringLiteral("max-compat");
  }
  return QStringLiteral("balanced");
}

QString toString(WebRtcErrorCode code) {
  switch (code) {
  case WebRtcErrorCode::InvalidConfiguration:
    return QStringLiteral("invalid_configuration");
  case WebRtcErrorCode::InvalidState:
    return QStringLiteral("invalid_state");
  case WebRtcErrorCode::BackendUnavailable:
    return QStringLiteral("backend_unavailable");
  case WebRtcErrorCode::BackendError:
    return QStringLiteral("backend_error");
  case WebRtcErrorCode::InvalidSessionDescription:
    return QStringLiteral("invalid_session_description");
  case WebRtcErrorCode::InvalidIceCandidate:
    return QStringLiteral("invalid_ice_candidate");
  case WebRtcErrorCode::Unsupported:
    return QStringLiteral("unsupported");
  case WebRtcErrorCode::Unknown:
    return QStringLiteral("unknown");
  }
  return QStringLiteral("unknown");
}

WebRtcSdpType webRtcSdpTypeFromString(const QString &value) {
  const QString type = normalized(value);
  if (type == QStringLiteral("pranswer")) {
    return WebRtcSdpType::Pranswer;
  }
  if (type == QStringLiteral("answer")) {
    return WebRtcSdpType::Answer;
  }
  if (type == QStringLiteral("rollback")) {
    return WebRtcSdpType::Rollback;
  }
  if (type == QStringLiteral("offer")) {
    return WebRtcSdpType::Offer;
  }
  return WebRtcSdpType::Invalid;
}

WebRtcIceTransportPolicy
webRtcIceTransportPolicyFromString(const QString &value) {
  return normalized(value) == QStringLiteral("relay")
             ? WebRtcIceTransportPolicy::Relay
             : WebRtcIceTransportPolicy::All;
}

WebRtcBundlePolicy webRtcBundlePolicyFromString(const QString &value) {
  const QString policy = normalized(value);
  if (policy == QStringLiteral("max-bundle")) {
    return WebRtcBundlePolicy::MaxBundle;
  }
  if (policy == QStringLiteral("max-compat")) {
    return WebRtcBundlePolicy::MaxCompat;
  }
  return WebRtcBundlePolicy::Balanced;
}

bool WebRtcIceServer::isValid(QString *reason) const {
  if (urls.isEmpty()) {
    return fail(reason,
                QStringLiteral("At least one ICE server URL is required."));
  }

  for (const QString &value : urls) {
    const QUrl url(value.trimmed());
    const QString scheme = normalized(url.scheme());
    if (url.isEmpty() || (scheme != QStringLiteral("stun") &&
                          scheme != QStringLiteral("stuns") &&
                          scheme != QStringLiteral("turn") &&
                          scheme != QStringLiteral("turns"))) {
      return fail(reason, QStringLiteral("ICE server URL must use stun, stuns, "
                                         "turn, or turns."));
    }
    // QUrl treats the standard opaque form (for example,
    // stun:stun.example.com) as a path rather than an authority. Accept both
    // that form and the //host form, but never accept embedded credentials.
    const QString endpoint = url.host().isEmpty() ? url.path() : url.host();
    if (endpoint.trimmed().isEmpty() || value.contains(QLatin1Char('@')) ||
        !url.userInfo().isEmpty() || !url.fragment().isEmpty() ||
        !isAllowedIceQuery(url)) {
      return fail(reason, QStringLiteral(
                              "ICE server URL must not contain credentials or "
                              "unsupported query parameters."));
    }
  }
  return true;
}

QJsonObject WebRtcIceServer::toJson() const {
  QJsonObject json;
  json.insert(QStringLiteral("urls"), QJsonArray::fromStringList(urls));
  if (!username.isEmpty()) {
    json.insert(QStringLiteral("username"), username);
  }
  if (!credential.isEmpty()) {
    json.insert(QStringLiteral("credential"), credential);
  }
  return json;
}

WebRtcIceServer WebRtcIceServer::fromJson(const QJsonObject &json) {
  WebRtcIceServer server;
  const QJsonValue urlsValue = json.value(QStringLiteral("urls"));
  if (urlsValue.isArray()) {
    for (const QJsonValue &url : urlsValue.toArray()) {
      if (url.isString()) {
        server.urls.append(url.toString());
      }
    }
  } else if (urlsValue.isString()) {
    server.urls.append(urlsValue.toString());
  }
  server.username = json.value(QStringLiteral("username")).toString();
  server.credential = json.value(QStringLiteral("credential")).toString();
  return server;
}

bool WebRtcConfiguration::isValid(QString *reason) const {
  if (iceCandidatePoolSize < 0 || iceCandidatePoolSize > 255) {
    return fail(
        reason,
        QStringLiteral("ICE candidate pool size must be between 0 and 255."));
  }
  for (const WebRtcIceServer &server : iceServers) {
    if (!server.isValid(reason)) {
      return false;
    }
  }
  return true;
}

QJsonObject WebRtcConfiguration::toJson() const {
  QJsonObject json;
  QJsonArray servers;
  for (const WebRtcIceServer &server : iceServers) {
    servers.append(server.toJson());
  }
  json.insert(QStringLiteral("iceServers"), servers);
  json.insert(QStringLiteral("iceTransportPolicy"),
              toString(iceTransportPolicy));
  json.insert(QStringLiteral("bundlePolicy"), toString(bundlePolicy));
  json.insert(QStringLiteral("iceCandidatePoolSize"), iceCandidatePoolSize);
  return json;
}

WebRtcConfiguration WebRtcConfiguration::fromJson(const QJsonObject &json) {
  WebRtcConfiguration configuration;
  for (const QJsonValue &value :
       json.value(QStringLiteral("iceServers")).toArray()) {
    if (value.isObject()) {
      configuration.iceServers.append(
          WebRtcIceServer::fromJson(value.toObject()));
    }
  }
  configuration.iceTransportPolicy = webRtcIceTransportPolicyFromString(
      json.value(QStringLiteral("iceTransportPolicy")).toString());
  configuration.bundlePolicy = webRtcBundlePolicyFromString(
      json.value(QStringLiteral("bundlePolicy")).toString());
  configuration.iceCandidatePoolSize =
      json.value(QStringLiteral("iceCandidatePoolSize")).toInt();
  return configuration;
}

bool WebRtcSessionDescription::isValid(QString *reason) const {
  if (type == WebRtcSdpType::Invalid) {
    return fail(reason, QStringLiteral("SDP type is invalid."));
  }
  if (type != WebRtcSdpType::Rollback && sdp.trimmed().isEmpty()) {
    return fail(reason, QStringLiteral("SDP must not be empty."));
  }
  return true;
}

QJsonObject WebRtcSessionDescription::toJson() const {
  return QJsonObject{{QStringLiteral("type"), toString(type)},
                     {QStringLiteral("sdp"), sdp}};
}

WebRtcSessionDescription
WebRtcSessionDescription::fromJson(const QJsonObject &json) {
  WebRtcSessionDescription description;
  description.type =
      webRtcSdpTypeFromString(json.value(QStringLiteral("type")).toString());
  description.sdp = json.value(QStringLiteral("sdp")).toString();
  return description;
}

bool WebRtcIceCandidate::isEndOfCandidates() const {
  return candidate.trimmed().isEmpty();
}

bool WebRtcIceCandidate::isValid(QString *reason) const {
  if (sdpMLineIndex < -1) {
    return fail(reason, QStringLiteral("sdpMLineIndex must be -1 or greater."));
  }
  if (isEndOfCandidates()) {
    return true;
  }
  if (!candidate.trimmed().startsWith(QStringLiteral("candidate:"),
                                      Qt::CaseInsensitive)) {
    return fail(reason, QStringLiteral("ICE candidate must start with "
                                       "candidate:."));
  }
  if (sdpMid.trimmed().isEmpty() && sdpMLineIndex < 0) {
    return fail(reason, QStringLiteral(
                            "ICE candidate requires sdpMid or sdpMLineIndex."));
  }
  return true;
}

QJsonObject WebRtcIceCandidate::toJson() const {
  QJsonObject json{{QStringLiteral("candidate"), candidate}};
  if (!sdpMid.isEmpty()) {
    json.insert(QStringLiteral("sdpMid"), sdpMid);
  }
  if (sdpMLineIndex >= 0) {
    json.insert(QStringLiteral("sdpMLineIndex"), sdpMLineIndex);
  }
  if (!usernameFragment.isEmpty()) {
    json.insert(QStringLiteral("usernameFragment"), usernameFragment);
  }
  return json;
}

WebRtcIceCandidate WebRtcIceCandidate::fromJson(const QJsonObject &json) {
  WebRtcIceCandidate candidate;
  candidate.candidate = json.value(QStringLiteral("candidate")).toString();
  candidate.sdpMid = json.contains(QStringLiteral("sdpMid"))
                         ? json.value(QStringLiteral("sdpMid")).toString()
                         : json.value(QStringLiteral("sdp_mid")).toString();
  candidate.sdpMLineIndex =
      json.contains(QStringLiteral("sdpMLineIndex"))
          ? json.value(QStringLiteral("sdpMLineIndex")).toInt(-1)
          : json.value(QStringLiteral("sdp_m_line_index")).toInt(-1);
  candidate.usernameFragment =
      json.contains(QStringLiteral("usernameFragment"))
          ? json.value(QStringLiteral("usernameFragment")).toString()
          : json.value(QStringLiteral("username_fragment")).toString();
  return candidate;
}

QJsonObject WebRtcTrackInfo::toJson() const {
  return QJsonObject{
      {QStringLiteral("kind"), toString(kind)},
      {QStringLiteral("id"), id},
      {QStringLiteral("label"), label},
      {QStringLiteral("streamIds"), QJsonArray::fromStringList(streamIds)},
      {QStringLiteral("enabled"), enabled}};
}

WebRtcTrackInfo WebRtcTrackInfo::fromJson(const QJsonObject &json) {
  WebRtcTrackInfo track;
  track.kind =
      mediaKindFromString(json.value(QStringLiteral("kind")).toString());
  track.id = json.value(QStringLiteral("id")).toString();
  track.label = json.value(QStringLiteral("label")).toString();
  for (const QJsonValue &streamId :
       json.value(QStringLiteral("streamIds")).toArray()) {
    if (streamId.isString()) {
      track.streamIds.append(streamId.toString());
    }
  }
  track.enabled = json.value(QStringLiteral("enabled")).toBool(true);
  return track;
}

} // namespace HubSight::Admin
