#include "../../include/hubsight/admin/webrtc/webrtc_client.h"

#include <QMetaType>

#include <utility>

namespace HubSight::Admin {

WebRtcClient::WebRtcClient(QObject *parent) : QObject(parent) {
  qRegisterMetaType<WebRtcError>();
  qRegisterMetaType<WebRtcPeerConnection *>();
}

WebRtcClient::~WebRtcClient() { closeAll(); }

WebRtcPeerConnection *WebRtcClient::createPeerConnection(
    const QString &sessionId, const WebRtcConfiguration &configuration,
    std::unique_ptr<WebRtcPeerConnectionBackend> backend) {
  const QString normalizedSessionId = sessionId.trimmed();
  if (normalizedSessionId.isEmpty()) {
    WebRtcError error;
    error.code = WebRtcErrorCode::InvalidConfiguration;
    error.message = QStringLiteral("A WebRTC session ID is required.");
    error.operation = QStringLiteral("webrtc.createPeerConnection");
    emit errorOccurred(error);
    return nullptr;
  }
  if (m_peerConnections.contains(normalizedSessionId)) {
    WebRtcError error;
    error.code = WebRtcErrorCode::InvalidState;
    error.message =
        QStringLiteral("A WebRTC peer connection already exists for "
                       "this session ID.");
    error.operation = QStringLiteral("webrtc.createPeerConnection");
    emit errorOccurred(error);
    return nullptr;
  }
  QString reason;
  if (!configuration.isValid(&reason)) {
    WebRtcError error;
    error.code = WebRtcErrorCode::InvalidConfiguration;
    error.message = reason;
    error.operation = QStringLiteral("webrtc.createPeerConnection");
    emit errorOccurred(error);
    return nullptr;
  }

  auto *peer = new WebRtcPeerConnection(configuration, this);
  connect(peer, &WebRtcPeerConnection::errorOccurred, this,
          [this, normalizedSessionId](const WebRtcError &error) {
            emit peerError(normalizedSessionId, error);
            emit errorOccurred(error);
          });
  connect(peer, &QObject::destroyed, this, [this, normalizedSessionId, peer]() {
    if (m_peerConnections.value(normalizedSessionId) == peer) {
      m_peerConnections.remove(normalizedSessionId);
    }
  });

  m_peerConnections.insert(normalizedSessionId, peer);
  if (backend && !peer->setBackend(std::move(backend))) {
    m_peerConnections.remove(normalizedSessionId);
    peer->deleteLater();
    return nullptr;
  }

  emit peerConnectionCreated(normalizedSessionId, peer);
  return peer;
}

WebRtcPeerConnection *
WebRtcClient::peerConnection(const QString &sessionId) const {
  return m_peerConnections.value(sessionId.trimmed(), nullptr);
}

QStringList WebRtcClient::sessionIds() const {
  QStringList ids = m_peerConnections.keys();
  ids.sort();
  return ids;
}

bool WebRtcClient::closePeerConnection(const QString &sessionId) {
  const QString normalizedSessionId = sessionId.trimmed();
  WebRtcPeerConnection *peer = m_peerConnections.take(normalizedSessionId);
  if (!peer) {
    return false;
  }
  peer->close();
  peer->deleteLater();
  emit peerConnectionClosed(normalizedSessionId);
  return true;
}

void WebRtcClient::closeAll() {
  const QStringList ids = sessionIds();
  for (const QString &id : ids) {
    closePeerConnection(id);
  }
}

bool WebRtcClient::setEndpointUrls(const QUrl &mediaBaseUrl,
                                   const QUrl &signalingUrl, int mediaPort) {
  if (!mediaBaseUrl.isValid() || mediaBaseUrl.host().isEmpty() ||
      !signalingUrl.isValid() || signalingUrl.host().isEmpty() ||
      mediaPort < 1 || mediaPort > 65535) {
    WebRtcError error;
    error.code = WebRtcErrorCode::InvalidConfiguration;
    error.message = QStringLiteral("WebRTC endpoint configuration is invalid.");
    error.operation = QStringLiteral("webrtc.configureEndpoints");
    emit errorOccurred(error);
    return false;
  }
  m_mediaBaseUrl = mediaBaseUrl;
  m_signalingUrl = signalingUrl;
  m_mediaPort = mediaPort;
  return true;
}

void WebRtcClient::clearEndpointUrls() {
  m_mediaBaseUrl = QUrl{};
  m_signalingUrl = QUrl{};
  m_mediaPort = 8555;
}

QUrl WebRtcClient::mediaBaseUrl() const { return m_mediaBaseUrl; }

QUrl WebRtcClient::signalingUrl() const { return m_signalingUrl; }

int WebRtcClient::mediaPort() const { return m_mediaPort; }

} // namespace HubSight::Admin
