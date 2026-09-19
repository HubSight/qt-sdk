#include "../../include/hubsight/admin/resources/live_client.h"

#include "../admin_transport.h"
#include "../resource_helpers.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QUrlQuery>

namespace HubSight::Admin {
namespace {

QByteArray jsonBody(const QJsonObject &object) {
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

AdminError invalidInput(const QString &operation, const QString &message) {
  AdminError error;
  error.category = ErrorCategory::Validation;
  error.serverCode = QStringLiteral("INVALID_INPUT");
  error.developerMessage = message;
  error.operation = operation;
  return error;
}

QString cameraPath(const QString &cameraId) {
  return QStringLiteral("/live/cameras/") +
         QString::fromUtf8(QUrl::toPercentEncoding(cameraId));
}

QJsonArray cameraArray(const QJsonObject &object) {
  const QJsonValue data = object.value(QStringLiteral("data"));
  if (data.isArray()) {
    return data.toArray();
  }
  const QJsonValue cameras = object.value(QStringLiteral("cameras"));
  if (cameras.isArray()) {
    return cameras.toArray();
  }
  const QJsonValue items = object.value(QStringLiteral("items"));
  return items.isArray() ? items.toArray() : QJsonArray{};
}

AdminError withLiveContext(AdminError error, const QString &sessionId,
                           const QString &cameraId) {
  QJsonObject details = error.details.toObject();
  if (!error.details.isUndefined() && !error.details.isNull() &&
      !error.details.isObject()) {
    details.insert(QStringLiteral("server_details"), error.details);
  }
  if (!sessionId.isEmpty()) {
    details.insert(QStringLiteral("session_id"), sessionId);
  }
  if (!cameraId.isEmpty()) {
    details.insert(QStringLiteral("camera_id"), cameraId);
  }
  error.details = details;
  return error;
}

QJsonObject responseObject(const QByteArray &body, bool *valid) {
  if (body.trimmed().isEmpty()) {
    if (valid) {
      *valid = true;
    }
    return {};
  }
  QJsonObject object;
  const bool decoded = Internal::decodeObject(body, &object);
  if (valid) {
    *valid = decoded;
  }
  return object;
}

} // namespace

LiveClient::LiveClient(AdminTransport *transport, QObject *parent)
    : QObject(parent), m_transport(transport) {
  qRegisterMetaType<LiveCapabilities>();
  qRegisterMetaType<LiveCameraPage>();
  qRegisterMetaType<LiveSession>();
  qRegisterMetaType<LiveSessionStats>();
  qRegisterMetaType<LiveCameraStatus>();

  connect(m_transport, &AdminTransport::allRequestsCanceled, this,
          [this]() { m_pending.clear(); });
  connect(m_transport, &AdminTransport::finished, this,
          [this](quint64 requestId, const TransportResponse &response) {
            handleResponse(requestId, response);
          });
}

void LiveClient::fetchCapabilities() {
  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = QStringLiteral("/live/capabilities");
  request.operation = QStringLiteral("live.capabilities");
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::Capabilities, request);
}

void LiveClient::listCameras(const QString &cursor, int limit) {
  const QString operation = QStringLiteral("live.cameras.list");
  if (limit < 1 || limit > 100) {
    sendError(invalidInput(
        operation, QStringLiteral("Live camera page limit must be between 1 "
                                  "and 100.")));
    return;
  }

  QUrlQuery query;
  query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
  if (!cursor.trimmed().isEmpty()) {
    query.addQueryItem(QStringLiteral("cursor"), cursor.trimmed());
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path =
      QStringLiteral("/live/cameras?") + query.toString(QUrl::FullyEncoded);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::Cameras, request);
}

void LiveClient::negotiate(const QString &cameraId, const QString &profile,
                           const QJsonObject &options) {
  const QString operation = QStringLiteral("live.sessions.negotiate");
  const QString normalizedCameraId = cameraId.trimmed();
  if (normalizedCameraId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Camera ID is required.")));
    return;
  }

  QJsonObject body = options;
  body.insert(QStringLiteral("camera_id"), normalizedCameraId);
  if (!profile.trimmed().isEmpty()) {
    body.insert(QStringLiteral("profile"), profile.trimmed());
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/live/sessions:negotiate");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(body);
  sendRequest(PendingKind::Negotiate, request, {}, normalizedCameraId);
}

void LiveClient::heartbeat(const QString &sessionId,
                           const QJsonObject &metrics) {
  const QString operation = QStringLiteral("live.sessions.heartbeat");
  const QString normalizedSessionId = sessionId.trimmed();
  if (normalizedSessionId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Session ID is required.")));
    return;
  }

  QJsonObject body = metrics;
  body.insert(QStringLiteral("session_id"), normalizedSessionId);

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/live/sessions:heartbeat");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(body);
  sendRequest(PendingKind::Heartbeat, request, normalizedSessionId);
}

void LiveClient::release(const QString &sessionId) {
  const QString operation = QStringLiteral("live.sessions.release");
  const QString normalizedSessionId = sessionId.trimmed();
  if (normalizedSessionId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Session ID is required.")));
    return;
  }

  const QJsonObject body{
      {QStringLiteral("session_id"), normalizedSessionId},
  };
  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/live/sessions:release");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(body);
  sendRequest(PendingKind::Release, request, normalizedSessionId);
}

void LiveClient::changeProfile(const QString &sessionId, const QString &profile,
                               const QJsonObject &options) {
  const QString operation = QStringLiteral("live.sessions.change_profile");
  const QString normalizedSessionId = sessionId.trimmed();
  const QString normalizedProfile = profile.trimmed();
  if (normalizedSessionId.isEmpty() || normalizedProfile.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("Session ID and profile are required.")));
    return;
  }

  QJsonObject body = options;
  body.insert(QStringLiteral("session_id"), normalizedSessionId);
  body.insert(QStringLiteral("profile"), normalizedProfile);

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/live/sessions:change-profile");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(body);
  sendRequest(PendingKind::ChangeProfile, request, normalizedSessionId);
}

void LiveClient::fetchSessionStats(const QString &sessionId) {
  const QString operation = QStringLiteral("live.sessions.stats");
  const QString normalizedSessionId = sessionId.trimmed();
  QUrlQuery query;
  if (!normalizedSessionId.isEmpty()) {
    query.addQueryItem(QStringLiteral("session_id"), normalizedSessionId);
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = QStringLiteral("/live/sessions:stats");
  if (!query.isEmpty()) {
    request.path += QStringLiteral("?") + query.toString(QUrl::FullyEncoded);
  }
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::SessionStats, request, normalizedSessionId);
}

void LiveClient::reportQoe(const QString &sessionId, const QJsonObject &qoe) {
  const QString operation = QStringLiteral("live.sessions.qoe");
  const QString normalizedSessionId = sessionId.trimmed();
  if (normalizedSessionId.isEmpty() || qoe.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("Session ID and QoE metrics are required.")));
    return;
  }

  QJsonObject body = qoe;
  body.insert(QStringLiteral("session_id"), normalizedSessionId);

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/live/sessions:qoe");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(body);
  sendRequest(PendingKind::Qoe, request, normalizedSessionId);
}

void LiveClient::fetchCameraStatus(const QString &cameraId) {
  const QString operation = QStringLiteral("live.camera.status");
  const QString normalizedCameraId = cameraId.trimmed();
  if (normalizedCameraId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Camera ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = cameraPath(normalizedCameraId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::CameraStatus, request, {}, normalizedCameraId);
}

void LiveClient::sendError(const AdminError &error) {
  emit errorOccurred(error);
}

void LiveClient::sendRequest(PendingKind kind, const TransportRequest &request,
                             const QString &sessionId,
                             const QString &cameraId) {
  const quint64 requestId = m_transport->send(request);
  if (requestId == 0) {
    sendError(
        withLiveContext(m_transport->configurationError(request.operation),
                        sessionId, cameraId));
    return;
  }
  m_pending.insert(requestId, {kind, sessionId, cameraId});
}

void LiveClient::handleResponse(quint64 requestId,
                                const TransportResponse &response) {
  const auto pending = m_pending.find(requestId);
  if (pending == m_pending.end()) {
    return;
  }
  const PendingRequest pendingRequest = pending.value();
  m_pending.erase(pending);

  if (!response.isHttpSuccess()) {
    sendError(withLiveContext(
        Internal::parseError(m_transport, response, response.operation),
        pendingRequest.sessionId, pendingRequest.cameraId));
    return;
  }

  bool validJson = false;
  const QJsonObject object = responseObject(response.body, &validJson);
  if (!validJson) {
    sendError(withLiveContext(Internal::invalidJson(response.operation),
                              pendingRequest.sessionId,
                              pendingRequest.cameraId));
    return;
  }

  emit operationCompleted(response.operation, object);

  switch (pendingRequest.kind) {
  case PendingKind::Capabilities: {
    const LiveCapabilities capabilities = LiveCapabilities::fromJson(object);
    if (!capabilities.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("live capabilities")));
      return;
    }
    emit capabilitiesReceived(capabilities);
    break;
  }
  case PendingKind::Cameras: {
    LiveCameraPage page;
    const QJsonArray values = cameraArray(object);
    page.items.reserve(values.size());
    for (const QJsonValue &value : values) {
      if (!value.isObject()) {
        sendError(Internal::invalidModel(response.operation,
                                         QStringLiteral("live camera list")));
        return;
      }
      const LiveCameraSummary camera =
          LiveCameraSummary::fromJson(value.toObject());
      if (!camera.isValid()) {
        sendError(Internal::invalidModel(response.operation,
                                         QStringLiteral("live camera list")));
        return;
      }
      page.items.append(camera);
    }
    page.nextCursor = object.value(QStringLiteral("next_cursor")).toString();
    if (page.nextCursor.isEmpty()) {
      page.nextCursor = object.value(QStringLiteral("nextCursor")).toString();
    }
    page.hasMore = object.value(QStringLiteral("has_more"))
                       .toBool(!page.nextCursor.isEmpty());
    emit camerasReceived(page);
    break;
  }
  case PendingKind::Negotiate: {
    LiveSession session = LiveSession::fromJson(object);
    if (session.sessionId.isEmpty()) {
      session.sessionId = pendingRequest.sessionId;
    }
    if (session.cameraId.isEmpty()) {
      session.cameraId = pendingRequest.cameraId;
    }
    if (!session.isValid()) {
      sendError(withLiveContext(
          Internal::invalidModel(response.operation,
                                 QStringLiteral("live session")),
          pendingRequest.sessionId, pendingRequest.cameraId));
      return;
    }
    emit sessionNegotiated(session);
    break;
  }
  case PendingKind::Heartbeat:
    emit sessionHeartbeatReceived(pendingRequest.sessionId, object);
    break;
  case PendingKind::Release:
    emit sessionReleased(pendingRequest.sessionId);
    break;
  case PendingKind::ChangeProfile: {
    LiveSession session = LiveSession::fromJson(object);
    if (session.sessionId.isEmpty()) {
      session.sessionId = pendingRequest.sessionId;
    }
    if (!session.isValid()) {
      sendError(withLiveContext(
          Internal::invalidModel(response.operation,
                                 QStringLiteral("live session")),
          pendingRequest.sessionId, pendingRequest.cameraId));
      return;
    }
    emit sessionProfileChanged(session);
    break;
  }
  case PendingKind::SessionStats: {
    const QJsonValue data = object.value(QStringLiteral("data"));
    if (data.isArray()) {
      for (const QJsonValue &value : data.toArray()) {
        if (!value.isObject()) {
          sendError(Internal::invalidModel(
              response.operation, QStringLiteral("live session stats")));
          return;
        }
        LiveSessionStats stats = LiveSessionStats::fromJson(value.toObject());
        if (stats.sessionId.isEmpty()) {
          stats.sessionId = pendingRequest.sessionId;
        }
        emit sessionStatsReceived(stats);
      }
      break;
    }
    LiveSessionStats stats = LiveSessionStats::fromJson(object);
    if (stats.sessionId.isEmpty()) {
      stats.sessionId = pendingRequest.sessionId;
    }
    if (!stats.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("live session stats")));
      return;
    }
    emit sessionStatsReceived(stats);
    break;
  }
  case PendingKind::Qoe:
    emit qoeReported(pendingRequest.sessionId, object);
    break;
  case PendingKind::CameraStatus: {
    LiveCameraStatus status = LiveCameraStatus::fromJson(object);
    if (status.cameraId.isEmpty()) {
      status.cameraId = pendingRequest.cameraId;
    }
    if (!status.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("live camera status")));
      return;
    }
    emit cameraStatusReceived(status);
    break;
  }
  }
}

} // namespace HubSight::Admin
