#pragma once

#include "../admin_export.h"
#include "webrtc_types.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

namespace HubSight::Admin {

// Adapter boundary for a real WebRTC implementation. Qt Network and
// Qt WebSockets do not provide an RTCPeerConnection/media engine, so the SDK
// deliberately keeps libdatachannel, native WebRTC, and platform decoders out
// of the core library. A Phase 2 adapter implements this interface and emits
// results asynchronously through the signals below.
class HUBSIGHT_ADMIN_EXPORT WebRtcPeerConnectionBackend : public QObject {
  Q_OBJECT

public:
  explicit WebRtcPeerConnectionBackend(QObject *parent = nullptr);
  ~WebRtcPeerConnectionBackend() override;

  virtual bool initialize(const WebRtcConfiguration &configuration) = 0;
  virtual bool createOffer(const QJsonObject &options = {}) = 0;
  virtual bool createAnswer(const QJsonObject &options = {}) = 0;
  virtual bool
  setLocalDescription(const WebRtcSessionDescription &description) = 0;
  virtual bool
  setRemoteDescription(const WebRtcSessionDescription &description) = 0;
  virtual bool addIceCandidate(const WebRtcIceCandidate &candidate) = 0;
  virtual bool restartIce() = 0;
  virtual void close() = 0;

signals:
  void localDescriptionCreated(
      HubSight::Admin::WebRtcSessionDescription description);
  void
  localDescriptionSet(HubSight::Admin::WebRtcSessionDescription description);
  void
  remoteDescriptionSet(HubSight::Admin::WebRtcSessionDescription description);
  void iceCandidateGenerated(HubSight::Admin::WebRtcIceCandidate candidate);
  void iceGatheringStateChanged(HubSight::Admin::WebRtcIceGatheringState state);
  void
  iceConnectionStateChanged(HubSight::Admin::WebRtcIceConnectionState state);
  void connectionStateChanged(HubSight::Admin::WebRtcPeerConnectionState state);
  void signalingStateChanged(HubSight::Admin::WebRtcSignalingState state);
  void trackReceived(HubSight::Admin::WebRtcTrackInfo track);
  void statsReceived(QJsonObject stats);
  void errorOccurred(HubSight::Admin::WebRtcError error);
};

// Backend-neutral RTCPeerConnection facade. It owns the adapter supplied by
// the application, tracks signaling/ICE state, and exposes standard SDP/ICE
// objects to the future live-session client.
class HUBSIGHT_ADMIN_EXPORT WebRtcPeerConnection final : public QObject {
  Q_OBJECT

public:
  explicit WebRtcPeerConnection(WebRtcConfiguration configuration = {},
                                QObject *parent = nullptr);
  ~WebRtcPeerConnection() override;

  WebRtcConfiguration configuration() const;
  bool isConfigurationValid() const;
  QString configurationError() const;

  // Replacing an adapter is allowed only while the connection is new. The
  // adapter must not already have a QObject parent and is owned by this object
  // after a successful call.
  bool setBackend(std::unique_ptr<WebRtcPeerConnectionBackend> backend);
  bool hasBackend() const;

  bool createOffer(const QJsonObject &options = {});
  bool createAnswer(const QJsonObject &options = {});
  bool setLocalDescription(const WebRtcSessionDescription &description);
  bool setRemoteDescription(const WebRtcSessionDescription &description);
  bool addIceCandidate(const WebRtcIceCandidate &candidate);
  bool restartIce();
  void close();

  WebRtcPeerConnectionState connectionState() const;
  WebRtcIceConnectionState iceConnectionState() const;
  WebRtcIceGatheringState iceGatheringState() const;
  WebRtcSignalingState signalingState() const;
  bool hasLocalDescription() const;
  bool hasRemoteDescription() const;
  WebRtcSessionDescription localDescription() const;
  WebRtcSessionDescription remoteDescription() const;
  QVector<WebRtcIceCandidate> remoteCandidates() const;

signals:
  void localDescriptionCreated(
      HubSight::Admin::WebRtcSessionDescription description);
  void localDescriptionChanged(
      HubSight::Admin::WebRtcSessionDescription description);
  void remoteDescriptionChanged(
      HubSight::Admin::WebRtcSessionDescription description);
  void iceCandidateGenerated(HubSight::Admin::WebRtcIceCandidate candidate);
  void iceCandidateAdded(HubSight::Admin::WebRtcIceCandidate candidate);
  void iceGatheringStateChanged(HubSight::Admin::WebRtcIceGatheringState state);
  void
  iceConnectionStateChanged(HubSight::Admin::WebRtcIceConnectionState state);
  void connectionStateChanged(HubSight::Admin::WebRtcPeerConnectionState state);
  void signalingStateChanged(HubSight::Admin::WebRtcSignalingState state);
  void trackReceived(HubSight::Admin::WebRtcTrackInfo track);
  void statsReceived(QJsonObject stats);
  void errorOccurred(HubSight::Admin::WebRtcError error);

private:
  bool ensureUsable(const QString &operation);
  bool emitValidationError(WebRtcErrorCode code, const QString &message,
                           const QString &operation);
  void connectBackendSignals();
  void setConnectionState(WebRtcPeerConnectionState state);
  void setIceConnectionState(WebRtcIceConnectionState state);
  void setIceGatheringState(WebRtcIceGatheringState state);
  void setSignalingState(WebRtcSignalingState state);
  void updateSignalingStateForLocal(WebRtcSdpType type);
  void updateSignalingStateForRemote(WebRtcSdpType type);

  WebRtcConfiguration m_configuration;
  QString m_configurationError;
  std::unique_ptr<WebRtcPeerConnectionBackend> m_backend;
  WebRtcPeerConnectionState m_connectionState = WebRtcPeerConnectionState::New;
  WebRtcIceConnectionState m_iceConnectionState = WebRtcIceConnectionState::New;
  WebRtcIceGatheringState m_iceGatheringState = WebRtcIceGatheringState::New;
  WebRtcSignalingState m_signalingState = WebRtcSignalingState::Stable;
  WebRtcSessionDescription m_localDescription;
  WebRtcSessionDescription m_remoteDescription;
  QVector<WebRtcIceCandidate> m_remoteCandidates;
  bool m_hasLocalDescription = false;
  bool m_hasRemoteDescription = false;
};

} // namespace HubSight::Admin
