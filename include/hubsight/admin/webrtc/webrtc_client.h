#pragma once

#include "../admin_export.h"
#include "webrtc_peer_connection.h"

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QUrl>

#include <memory>

namespace HubSight::Admin {

// Session registry used by the future live REST/signaling client. It does not
// perform signaling or media transport itself; it gives Phase 2 a stable place
// to bind a negotiated live session to one peer connection.
class HUBSIGHT_ADMIN_EXPORT WebRtcClient final : public QObject {
  Q_OBJECT

public:
  explicit WebRtcClient(QObject *parent = nullptr);
  ~WebRtcClient() override;

  WebRtcPeerConnection *createPeerConnection(
      const QString &sessionId, const WebRtcConfiguration &configuration = {},
      std::unique_ptr<WebRtcPeerConnectionBackend> backend = nullptr);
  WebRtcPeerConnection *peerConnection(const QString &sessionId) const;
  QStringList sessionIds() const;
  bool closePeerConnection(const QString &sessionId);
  void closeAll();

  // Endpoint source supplied by the Admin .hscfg importer. A media engine
  // adapter may use these values when it is attached in a later phase.
  bool setEndpointUrls(const QUrl &mediaBaseUrl, const QUrl &signalingUrl,
                       int mediaPort = 8555);
  void clearEndpointUrls();
  QUrl mediaBaseUrl() const;
  QUrl signalingUrl() const;
  int mediaPort() const;

signals:
  void peerConnectionCreated(QString sessionId,
                             HubSight::Admin::WebRtcPeerConnection *peer);
  void peerConnectionClosed(QString sessionId);
  void peerError(QString sessionId, HubSight::Admin::WebRtcError error);
  void errorOccurred(HubSight::Admin::WebRtcError error);

private:
  QHash<QString, WebRtcPeerConnection *> m_peerConnections;
  QUrl m_mediaBaseUrl;
  QUrl m_signalingUrl;
  int m_mediaPort = 8555;
};

} // namespace HubSight::Admin

Q_DECLARE_METATYPE(HubSight::Admin::WebRtcPeerConnection *)
