#include "../../include/hubsight/admin/webrtc/webrtc_peer_connection.h"

#include <QMetaType>

#include <utility>

namespace HubSight::Admin {

WebRtcPeerConnectionBackend::WebRtcPeerConnectionBackend(QObject *parent)
    : QObject(parent) {}

WebRtcPeerConnectionBackend::~WebRtcPeerConnectionBackend() = default;

WebRtcPeerConnection::WebRtcPeerConnection(WebRtcConfiguration configuration,
                                           QObject *parent)
    : QObject(parent), m_configuration(std::move(configuration)) {
  qRegisterMetaType<WebRtcError>();
  qRegisterMetaType<WebRtcSessionDescription>();
  qRegisterMetaType<WebRtcIceCandidate>();
  qRegisterMetaType<WebRtcTrackInfo>();
  qRegisterMetaType<WebRtcPeerConnectionState>();
  qRegisterMetaType<WebRtcIceConnectionState>();
  qRegisterMetaType<WebRtcIceGatheringState>();
  qRegisterMetaType<WebRtcSignalingState>();

  if (!m_configuration.isValid(&m_configurationError)) {
    emitValidationError(WebRtcErrorCode::InvalidConfiguration,
                        m_configurationError,
                        QStringLiteral("webrtc.configure"));
  }
}

WebRtcPeerConnection::~WebRtcPeerConnection() {
  if (m_connectionState != WebRtcPeerConnectionState::Closed) {
    close();
  }
}

WebRtcConfiguration WebRtcPeerConnection::configuration() const {
  return m_configuration;
}

bool WebRtcPeerConnection::isConfigurationValid() const {
  return m_configurationError.isEmpty();
}

QString WebRtcPeerConnection::configurationError() const {
  return m_configurationError;
}

bool WebRtcPeerConnection::setBackend(
    std::unique_ptr<WebRtcPeerConnectionBackend> backend) {
  if (m_connectionState != WebRtcPeerConnectionState::New || m_backend) {
    return emitValidationError(
        WebRtcErrorCode::InvalidState,
        QStringLiteral("A WebRTC backend can only be attached once while the "
                       "peer connection is new."),
        QStringLiteral("webrtc.setBackend"));
  }
  if (!backend) {
    return emitValidationError(WebRtcErrorCode::BackendUnavailable,
                               QStringLiteral("The WebRTC backend is null."),
                               QStringLiteral("webrtc.setBackend"));
  }
  if (backend->parent() != nullptr) {
    return emitValidationError(
        WebRtcErrorCode::InvalidState,
        QStringLiteral("The WebRTC backend must not already have a QObject "
                       "parent because the peer connection owns it."),
        QStringLiteral("webrtc.setBackend"));
  }
  if (!isConfigurationValid()) {
    return emitValidationError(WebRtcErrorCode::InvalidConfiguration,
                               m_configurationError,
                               QStringLiteral("webrtc.setBackend"));
  }

  m_backend = std::move(backend);
  connectBackendSignals();
  if (!m_backend->initialize(m_configuration)) {
    emitValidationError(
        WebRtcErrorCode::BackendError,
        QStringLiteral("The WebRTC backend rejected its configuration."),
        QStringLiteral("webrtc.initialize"));
    m_backend.reset();
    return false;
  }
  return true;
}

bool WebRtcPeerConnection::hasBackend() const { return m_backend != nullptr; }

bool WebRtcPeerConnection::createOffer(const QJsonObject &options) {
  if (!ensureUsable(QStringLiteral("webrtc.createOffer"))) {
    return false;
  }
  if (!m_backend->createOffer(options)) {
    emitValidationError(WebRtcErrorCode::BackendError,
                        QStringLiteral("The WebRTC backend could not create an "
                                       "offer."),
                        QStringLiteral("webrtc.createOffer"));
    return false;
  }
  return true;
}

bool WebRtcPeerConnection::createAnswer(const QJsonObject &options) {
  if (!ensureUsable(QStringLiteral("webrtc.createAnswer"))) {
    return false;
  }
  if (!m_backend->createAnswer(options)) {
    emitValidationError(WebRtcErrorCode::BackendError,
                        QStringLiteral("The WebRTC backend could not create an "
                                       "answer."),
                        QStringLiteral("webrtc.createAnswer"));
    return false;
  }
  return true;
}

bool WebRtcPeerConnection::setLocalDescription(
    const WebRtcSessionDescription &description) {
  QString reason;
  if (!description.isValid(&reason)) {
    return emitValidationError(WebRtcErrorCode::InvalidSessionDescription,
                               reason,
                               QStringLiteral("webrtc.setLocalDescription"));
  }
  if (!ensureUsable(QStringLiteral("webrtc.setLocalDescription"))) {
    return false;
  }
  if (!m_backend->setLocalDescription(description)) {
    emitValidationError(
        WebRtcErrorCode::BackendError,
        QStringLiteral("The WebRTC backend rejected the local description."),
        QStringLiteral("webrtc.setLocalDescription"));
    return false;
  }
  return true;
}

bool WebRtcPeerConnection::setRemoteDescription(
    const WebRtcSessionDescription &description) {
  QString reason;
  if (!description.isValid(&reason)) {
    return emitValidationError(WebRtcErrorCode::InvalidSessionDescription,
                               reason,
                               QStringLiteral("webrtc.setRemoteDescription"));
  }
  if (!ensureUsable(QStringLiteral("webrtc.setRemoteDescription"))) {
    return false;
  }
  if (!m_backend->setRemoteDescription(description)) {
    emitValidationError(
        WebRtcErrorCode::BackendError,
        QStringLiteral("The WebRTC backend rejected the remote description."),
        QStringLiteral("webrtc.setRemoteDescription"));
    return false;
  }
  return true;
}

bool WebRtcPeerConnection::addIceCandidate(
    const WebRtcIceCandidate &candidate) {
  QString reason;
  if (!candidate.isValid(&reason)) {
    return emitValidationError(WebRtcErrorCode::InvalidIceCandidate, reason,
                               QStringLiteral("webrtc.addIceCandidate"));
  }
  if (!ensureUsable(QStringLiteral("webrtc.addIceCandidate"))) {
    return false;
  }
  if (!m_backend->addIceCandidate(candidate)) {
    emitValidationError(WebRtcErrorCode::BackendError,
                        QStringLiteral("The WebRTC backend rejected the ICE "
                                       "candidate."),
                        QStringLiteral("webrtc.addIceCandidate"));
    return false;
  }
  m_remoteCandidates.append(candidate);
  emit iceCandidateAdded(candidate);
  return true;
}

bool WebRtcPeerConnection::restartIce() {
  if (!ensureUsable(QStringLiteral("webrtc.restartIce"))) {
    return false;
  }
  if (!m_backend->restartIce()) {
    emitValidationError(WebRtcErrorCode::BackendError,
                        QStringLiteral("The WebRTC backend could not restart "
                                       "ICE."),
                        QStringLiteral("webrtc.restartIce"));
    return false;
  }
  return true;
}

void WebRtcPeerConnection::close() {
  if (m_connectionState == WebRtcPeerConnectionState::Closed) {
    return;
  }
  if (m_backend) {
    m_backend->close();
  }
  setConnectionState(WebRtcPeerConnectionState::Closed);
  setIceConnectionState(WebRtcIceConnectionState::Closed);
  setSignalingState(WebRtcSignalingState::Closed);
}

WebRtcPeerConnectionState WebRtcPeerConnection::connectionState() const {
  return m_connectionState;
}

WebRtcIceConnectionState WebRtcPeerConnection::iceConnectionState() const {
  return m_iceConnectionState;
}

WebRtcIceGatheringState WebRtcPeerConnection::iceGatheringState() const {
  return m_iceGatheringState;
}

WebRtcSignalingState WebRtcPeerConnection::signalingState() const {
  return m_signalingState;
}

bool WebRtcPeerConnection::hasLocalDescription() const {
  return m_hasLocalDescription;
}

bool WebRtcPeerConnection::hasRemoteDescription() const {
  return m_hasRemoteDescription;
}

WebRtcSessionDescription WebRtcPeerConnection::localDescription() const {
  return m_localDescription;
}

WebRtcSessionDescription WebRtcPeerConnection::remoteDescription() const {
  return m_remoteDescription;
}

QVector<WebRtcIceCandidate> WebRtcPeerConnection::remoteCandidates() const {
  return m_remoteCandidates;
}

bool WebRtcPeerConnection::ensureUsable(const QString &operation) {
  if (!isConfigurationValid()) {
    return emitValidationError(WebRtcErrorCode::InvalidConfiguration,
                               m_configurationError, operation);
  }
  if (m_connectionState == WebRtcPeerConnectionState::Closed) {
    return emitValidationError(
        WebRtcErrorCode::InvalidState,
        QStringLiteral("The WebRTC peer connection is closed."), operation);
  }
  if (!m_backend) {
    return emitValidationError(
        WebRtcErrorCode::BackendUnavailable,
        QStringLiteral("No WebRTC backend is installed. Attach a native or "
                       "libdatachannel adapter before using the peer "
                       "connection."),
        operation);
  }
  return true;
}

bool WebRtcPeerConnection::emitValidationError(WebRtcErrorCode code,
                                               const QString &message,
                                               const QString &operation) {
  WebRtcError error;
  error.code = code;
  error.message = message;
  error.operation = operation;
  error.retryable = code == WebRtcErrorCode::BackendUnavailable ||
                    code == WebRtcErrorCode::BackendError;
  emit errorOccurred(error);
  return false;
}

void WebRtcPeerConnection::connectBackendSignals() {
  connect(m_backend.get(),
          &WebRtcPeerConnectionBackend::localDescriptionCreated, this,
          [this](const WebRtcSessionDescription &description) {
            emit localDescriptionCreated(description);
          });
  connect(m_backend.get(), &WebRtcPeerConnectionBackend::localDescriptionSet,
          this, [this](const WebRtcSessionDescription &description) {
            m_localDescription = description;
            m_hasLocalDescription = true;
            updateSignalingStateForLocal(description.type);
            emit localDescriptionChanged(description);
          });
  connect(m_backend.get(), &WebRtcPeerConnectionBackend::remoteDescriptionSet,
          this, [this](const WebRtcSessionDescription &description) {
            m_remoteDescription = description;
            m_hasRemoteDescription = true;
            updateSignalingStateForRemote(description.type);
            emit remoteDescriptionChanged(description);
          });
  connect(m_backend.get(), &WebRtcPeerConnectionBackend::iceCandidateGenerated,
          this, [this](const WebRtcIceCandidate &candidate) {
            emit iceCandidateGenerated(candidate);
          });
  connect(
      m_backend.get(), &WebRtcPeerConnectionBackend::iceGatheringStateChanged,
      this,
      [this](WebRtcIceGatheringState state) { setIceGatheringState(state); });
  connect(
      m_backend.get(), &WebRtcPeerConnectionBackend::iceConnectionStateChanged,
      this,
      [this](WebRtcIceConnectionState state) { setIceConnectionState(state); });
  connect(m_backend.get(), &WebRtcPeerConnectionBackend::connectionStateChanged,
          this, [this](WebRtcPeerConnectionState state) {
            setConnectionState(state);
          });
  connect(m_backend.get(), &WebRtcPeerConnectionBackend::signalingStateChanged,
          this,
          [this](WebRtcSignalingState state) { setSignalingState(state); });
  connect(m_backend.get(), &WebRtcPeerConnectionBackend::trackReceived, this,
          [this](const WebRtcTrackInfo &track) { emit trackReceived(track); });
  connect(m_backend.get(), &WebRtcPeerConnectionBackend::statsReceived, this,
          [this](const QJsonObject &stats) { emit statsReceived(stats); });
  connect(m_backend.get(), &WebRtcPeerConnectionBackend::errorOccurred, this,
          [this](const WebRtcError &error) { emit errorOccurred(error); });
}

void WebRtcPeerConnection::setConnectionState(WebRtcPeerConnectionState state) {
  if (m_connectionState == state) {
    return;
  }
  m_connectionState = state;
  emit connectionStateChanged(state);
}

void WebRtcPeerConnection::setIceConnectionState(
    WebRtcIceConnectionState state) {
  if (m_iceConnectionState == state) {
    return;
  }
  m_iceConnectionState = state;
  emit iceConnectionStateChanged(state);
}

void WebRtcPeerConnection::setIceGatheringState(WebRtcIceGatheringState state) {
  if (m_iceGatheringState == state) {
    return;
  }
  m_iceGatheringState = state;
  emit iceGatheringStateChanged(state);
}

void WebRtcPeerConnection::setSignalingState(WebRtcSignalingState state) {
  if (m_signalingState == state) {
    return;
  }
  m_signalingState = state;
  emit signalingStateChanged(state);
}

void WebRtcPeerConnection::updateSignalingStateForLocal(WebRtcSdpType type) {
  switch (type) {
  case WebRtcSdpType::Offer:
    setSignalingState(WebRtcSignalingState::HaveLocalOffer);
    break;
  case WebRtcSdpType::Answer:
  case WebRtcSdpType::Pranswer:
  case WebRtcSdpType::Rollback:
    setSignalingState(WebRtcSignalingState::Stable);
    break;
  case WebRtcSdpType::Invalid:
    break;
  }
}

void WebRtcPeerConnection::updateSignalingStateForRemote(WebRtcSdpType type) {
  switch (type) {
  case WebRtcSdpType::Offer:
    setSignalingState(WebRtcSignalingState::HaveRemoteOffer);
    break;
  case WebRtcSdpType::Answer:
  case WebRtcSdpType::Pranswer:
  case WebRtcSdpType::Rollback:
    setSignalingState(WebRtcSignalingState::Stable);
    break;
  case WebRtcSdpType::Invalid:
    break;
  }
}

} // namespace HubSight::Admin
