#include "../../include/hubsight/admin/resources/live_types.h"

#include <QJsonArray>

#include <initializer_list>

namespace HubSight::Admin {
namespace {

QJsonObject nestedData(const QJsonObject &json) {
  const QJsonValue data = json.value(QStringLiteral("data"));
  return data.isObject() ? data.toObject() : json;
}

QStringList stringList(const QJsonValue &value) {
  QStringList result;
  if (!value.isArray()) {
    return result;
  }
  for (const QJsonValue &item : value.toArray()) {
    if (item.isString()) {
      result.append(item.toString());
    }
  }
  return result;
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

QDateTime parseTimestamp(const QJsonValue &value) {
  if (!value.isString()) {
    return {};
  }
  const QString input = value.toString();
  QDateTime result = QDateTime::fromString(input, Qt::ISODateWithMs);
  if (!result.isValid()) {
    result = QDateTime::fromString(input, Qt::ISODate);
  }
  return result.toUTC();
}

QJsonObject webRtcObject(const QJsonObject &source) {
  QJsonObject configuration = source.value(QStringLiteral("webrtc")).toObject();
  if (configuration.isEmpty()) {
    configuration = source.value(QStringLiteral("rtc")).toObject();
  }
  if (configuration.isEmpty()) {
    configuration = source.value(QStringLiteral("web_rtc")).toObject();
  }
  if (configuration.isEmpty()) {
    configuration = source;
  }

  // The Admin API catalog uses snake_case while the WebRTC DTO mirrors the
  // browser-facing camelCase shape. Normalize only the known fields here.
  const auto copyAlias = [&configuration, &source](const char *camel,
                                                   const char *snake) {
    const QString camelKey = QString::fromLatin1(camel);
    if (!configuration.contains(camelKey) &&
        source.contains(QString::fromLatin1(snake))) {
      configuration.insert(camelKey, source.value(QString::fromLatin1(snake)));
    }
  };
  copyAlias("iceServers", "ice_servers");
  copyAlias("iceTransportPolicy", "ice_transport_policy");
  copyAlias("bundlePolicy", "bundle_policy");
  copyAlias("iceCandidatePoolSize", "ice_candidate_pool_size");
  return configuration;
}

WebRtcSessionDescription parseRemoteDescription(const QJsonObject &source,
                                                bool *present) {
  QJsonObject description =
      source.value(QStringLiteral("remote_description")).toObject();
  if (description.isEmpty()) {
    description = source.value(QStringLiteral("remoteDescription")).toObject();
  }
  if (description.isEmpty()) {
    description = source.value(QStringLiteral("answer")).toObject();
  }
  if (description.isEmpty() && source.value(QStringLiteral("sdp")).isString()) {
    description.insert(QStringLiteral("sdp"),
                       source.value(QStringLiteral("sdp")));
    description.insert(
        QStringLiteral("type"),
        firstString(source, {"sdp_type", "sdpType", "type"}).isEmpty()
            ? QStringLiteral("answer")
            : firstString(source, {"sdp_type", "sdpType", "type"}));
  }
  const WebRtcSessionDescription result =
      WebRtcSessionDescription::fromJson(description);
  if (present) {
    *present = !description.isEmpty() && result.isValid();
  }
  return result;
}

} // namespace

bool LiveCapabilities::isValid() const {
  return !raw.isEmpty() && webRtcConfiguration.isValid();
}

LiveCapabilities LiveCapabilities::fromJson(const QJsonObject &json) {
  const QJsonObject source = nestedData(json);
  LiveCapabilities result;
  result.maxConcurrentSessions =
      source.value(QStringLiteral("max_concurrent_sessions"))
          .toInt(source.value(QStringLiteral("max_sessions")).toInt());
  result.supportedProfiles =
      stringList(source.contains(QStringLiteral("supported_profiles"))
                     ? source.value(QStringLiteral("supported_profiles"))
                     : source.value(QStringLiteral("profiles")));
  result.supportedTransports =
      stringList(source.value(QStringLiteral("supported_transports")));
  result.supportedCodecs =
      stringList(source.value(QStringLiteral("supported_codecs")));
  result.signalingTransport =
      firstString(source, {"signaling_transport", "signalingTransport"});
  result.mediaTransport =
      firstString(source, {"media_transport", "mediaTransport"});
  result.webRtcConfiguration =
      WebRtcConfiguration::fromJson(webRtcObject(source));
  result.webRtcSupported =
      firstBool(source, {"webrtc_supported", "web_rtc_supported", "webrtc"},
                !result.webRtcConfiguration.iceServers.isEmpty() ||
                    result.mediaTransport.compare(QStringLiteral("webrtc"),
                                                  Qt::CaseInsensitive) == 0);
  result.trickleIceSupported = firstBool(
      source, {"trickle_ice", "trickle_ice_supported", "trickleIce"}, true);
  result.requestId = json.value(QStringLiteral("request_id")).toString();
  result.raw = source;
  return result;
}

bool LiveCameraSummary::isValid() const { return !id.trimmed().isEmpty(); }

LiveCameraSummary LiveCameraSummary::fromJson(const QJsonObject &json) {
  const QJsonObject source = nestedData(json);
  LiveCameraSummary result;
  result.id = firstString(source, {"id", "camera_id"});
  result.name = source.value(QStringLiteral("name")).toString();
  result.online = firstBool(source, {"online", "is_online"}, false);
  result.available =
      firstBool(source, {"available", "is_available"}, result.online);
  result.supportedProfiles =
      stringList(source.contains(QStringLiteral("supported_profiles"))
                     ? source.value(QStringLiteral("supported_profiles"))
                     : source.value(QStringLiteral("profiles")));
  result.raw = source;
  return result;
}

bool LiveSession::isValid() const {
  if (sessionId.trimmed().isEmpty() || !webRtcConfiguration.isValid()) {
    return false;
  }
  if (hasRemoteDescription && !remoteDescription.isValid()) {
    return false;
  }
  for (const WebRtcIceCandidate &candidate : remoteCandidates) {
    if (!candidate.isValid()) {
      return false;
    }
  }
  return true;
}

LiveSession LiveSession::fromJson(const QJsonObject &json) {
  const QJsonObject source = nestedData(json);
  LiveSession result;
  result.sessionId = firstString(source, {"session_id", "sessionId", "id"});
  result.cameraId = firstString(source, {"camera_id", "cameraId"});
  result.profile = source.value(QStringLiteral("profile")).toString();
  result.state = source.value(QStringLiteral("state")).toString();
  result.mediaTransport =
      firstString(source, {"media_transport", "mediaTransport", "transport"});
  result.expiresAt =
      parseTimestamp(source.contains(QStringLiteral("expires_at"))
                         ? source.value(QStringLiteral("expires_at"))
                         : source.value(QStringLiteral("expiresAt")));
  result.webRtcConfiguration =
      WebRtcConfiguration::fromJson(webRtcObject(source));
  result.remoteDescription =
      parseRemoteDescription(source, &result.hasRemoteDescription);
  const QJsonValue candidates =
      source.contains(QStringLiteral("ice_candidates"))
          ? source.value(QStringLiteral("ice_candidates"))
          : source.value(QStringLiteral("iceCandidates"));
  for (const QJsonValue &value : candidates.toArray()) {
    if (value.isObject()) {
      result.remoteCandidates.append(
          WebRtcIceCandidate::fromJson(value.toObject()));
    }
  }
  result.signaling = source.value(QStringLiteral("signaling")).toObject();
  result.raw = source;
  return result;
}

bool LiveSessionStats::isValid() const { return !raw.isEmpty(); }

LiveSessionStats LiveSessionStats::fromJson(const QJsonObject &json) {
  const QJsonObject source = nestedData(json);
  LiveSessionStats result;
  result.sessionId = firstString(source, {"session_id", "sessionId", "id"});
  result.collectedAt =
      parseTimestamp(source.contains(QStringLiteral("collected_at"))
                         ? source.value(QStringLiteral("collected_at"))
                         : source.value(QStringLiteral("timestamp")));
  result.values = source.value(QStringLiteral("stats")).toObject();
  if (result.values.isEmpty()) {
    result.values = source.value(QStringLiteral("values")).toObject();
  }
  if (result.values.isEmpty()) {
    result.values = source;
  }
  result.raw = source;
  return result;
}

bool LiveCameraStatus::isValid() const { return !cameraId.trimmed().isEmpty(); }

LiveCameraStatus LiveCameraStatus::fromJson(const QJsonObject &json) {
  const QJsonObject source = nestedData(json);
  LiveCameraStatus result;
  result.cameraId = firstString(source, {"camera_id", "cameraId", "id"});
  result.status = source.value(QStringLiteral("status")).toString();
  result.online = firstBool(source, {"online", "is_online"},
                            result.status.compare(QStringLiteral("online"),
                                                  Qt::CaseInsensitive) == 0);
  result.available =
      firstBool(source, {"available", "is_available"}, result.online);
  result.details = source.value(QStringLiteral("details")).toObject();
  result.raw = source;
  return result;
}

} // namespace HubSight::Admin
