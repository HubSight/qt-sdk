#pragma once

#include "../admin_export.h"

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaType>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

class QWebSocket;
class QTimer;

namespace HubSight::Admin {

enum class SocketIoErrorCode {
  InvalidConfiguration,
  ConnectionFailed,
  HandshakeFailed,
  ProtocolError,
  Timeout,
  Canceled,
  Unsupported,
  Unknown,
};

struct HUBSIGHT_ADMIN_EXPORT SocketIoError {
  SocketIoErrorCode code = SocketIoErrorCode::Unknown;
  QString message;
  int transportError = 0;
  bool retryable = false;
};

// A transport-level Socket.IO v4 client foundation. Domain events and replay
// policy belong in higher-level realtime clients.
class HUBSIGHT_ADMIN_EXPORT SocketIoClient final : public QObject {
  Q_OBJECT

public:
  enum class State {
    Disconnected,
    Connecting,
    Reconnecting,
    Connected,
  };
  Q_ENUM(State)

  explicit SocketIoClient(QObject *parent = nullptr);
  ~SocketIoClient() override;

  // Accepts HTTPS/WSS in production. HTTP/WS are allowed only for loopback
  // tests and are converted to WS. Credentials in URL user-info/query/fragment
  // are not accepted; use setHeader() or setAuth() instead.
  bool setBaseUrl(const QUrl &url);
  QUrl baseUrl() const;
  void clearConfiguration();

  // The default Socket.IO path is /socket.io/. A deployment-specific path can
  // be configured, for example /relay/admin/v1/socket.io/.
  bool setPath(const QString &path);
  QString path() const;

  // Socket.IO namespace, with / representing the default namespace.
  bool setNamespace(const QString &namespaceName);
  QString namespaceName() const;

  // Auth is sent in the Socket.IO namespace CONNECT packet, not in the URL.
  void setAuth(const QJsonObject &auth);
  QJsonObject auth() const;

  // Headers are sent in the WebSocket opening handshake. This is the hook used
  // by AdminClient for X-API-Key and Authorization without query credentials.
  void setHeader(const QByteArray &name, const QByteArray &value);
  void removeHeader(const QByteArray &name);
  void clearHeaders();

  void setReconnectEnabled(bool enabled);
  bool reconnectEnabled() const;
  void setReconnectDelays(int initialDelayMs, int maximumDelayMs);
  void setMaxReconnectAttempts(int maximumAttempts);
  void setHandshakeTimeout(int timeoutMs);

  State state() const;
  bool isConnected() const;
  QString sessionId() const;

  void connectToServer();
  // Reconnects only after the current WebSocket has emitted disconnected().
  void reconnect();
  void disconnectFromServer(const QString &reason = {});

  // Sends a Socket.IO event. Each item in arguments is encoded as one event
  // argument. The returned id is non-zero only when an acknowledgement was
  // requested and can be matched through acknowledgementReceived().
  quint64 emitEvent(const QString &event, const QJsonArray &arguments = {},
                    bool requestAcknowledgement = false);

  // A server event may carry an acknowledgement id. Applications can reply
  // explicitly after handling eventAcknowledgementRequested().
  bool acknowledge(quint64 acknowledgementId, const QJsonArray &arguments = {});

signals:
  void stateChanged(HubSight::Admin::SocketIoClient::State state);
  void connected(QString namespaceName, QString sessionId);
  void disconnected(QString reason);
  void eventReceived(QString event, QJsonArray arguments);
  void eventAcknowledgementRequested(quint64 acknowledgementId, QString event,
                                     QJsonArray arguments);
  void acknowledgementReceived(quint64 acknowledgementId, QJsonArray arguments);
  void errorOccurred(HubSight::Admin::SocketIoError error);
  void reconnectScheduled(int attempt, int delayMs);

private:
  struct ParsedPacket {
    int type = -1;
    QString namespaceName = QStringLiteral("/");
    quint64 acknowledgementId = 0;
    bool hasAcknowledgementId = false;
    QByteArray payload;
  };

  QUrl websocketUrl() const;
  QByteArray namespaceConnectPacket() const;
  QByteArray namespacePrefix() const;

  void startConnection(bool reconnecting);
  void handleSocketConnected();
  void handleSocketDisconnected();
  void handleTransportError(int error, const QString &message);
  void handleTextMessage(const QString &message);
  void handleBinaryMessage(const QByteArray &message);
  void handleEngineOpen(const QByteArray &payload);
  void handleEngineMessage(const QByteArray &payload);
  void handleSocketPacket(const QByteArray &payload);
  bool parseSocketPacket(const QByteArray &payload, ParsedPacket *packet,
                         QString *error) const;
  void handleNamespaceConnect(const ParsedPacket &packet);
  void handleNamespaceDisconnect(const ParsedPacket &packet);
  void handleSocketEvent(const ParsedPacket &packet);
  void handleSocketAcknowledgement(const ParsedPacket &packet);
  void handleSocketError(const ParsedPacket &packet);

  void sendEnginePong();
  bool sendText(const QByteArray &message);
  void restartHeartbeatTimer();
  void handleHandshakeTimeout();
  void handleHeartbeatTimeout();
  void scheduleReconnect();
  void setState(State state);
  void emitError(SocketIoErrorCode code, const QString &message, bool retryable,
                 int transportError = 0);
  void closeTransport(const QString &reason);
  void disconnectInternal(const QString &reason, bool reconnectAfterDisconnect);

  QWebSocket *m_socket = nullptr;
  QTimer *m_reconnectTimer = nullptr;
  QTimer *m_handshakeTimer = nullptr;
  QTimer *m_heartbeatTimer = nullptr;

  QUrl m_baseUrl;
  QString m_path = QStringLiteral("/socket.io/");
  QString m_namespace = QStringLiteral("/");
  QJsonObject m_auth;
  QHash<QByteArray, QByteArray> m_headers;

  State m_state = State::Disconnected;
  QString m_sessionId;
  int m_pingIntervalMs = 25000;
  int m_pingTimeoutMs = 20000;
  int m_handshakeTimeoutMs = 10000;
  int m_reconnectInitialDelayMs = 1000;
  int m_reconnectMaximumDelayMs = 30000;
  int m_maxReconnectAttempts = -1;
  int m_reconnectAttempt = 0;
  quint64 m_nextAcknowledgementId = 1;
  QSet<quint64> m_pendingAcknowledgements;
  QSet<quint64> m_requestedAcknowledgements;
  QString m_lastDisconnectReason;
  bool m_reconnectEnabled = true;
  bool m_manualDisconnect = true;
  bool m_engineOpened = false;
  bool m_namespaceConnected = false;
  bool m_transportErrorReported = false;
  bool m_reconnectAfterDisconnect = false;
};

} // namespace HubSight::Admin

Q_DECLARE_METATYPE(HubSight::Admin::SocketIoErrorCode)
Q_DECLARE_METATYPE(HubSight::Admin::SocketIoError)
Q_DECLARE_METATYPE(HubSight::Admin::SocketIoClient::State)
