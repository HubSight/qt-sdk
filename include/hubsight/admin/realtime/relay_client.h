#pragma once

#include "../admin_export.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QUrl>

class QTimer;
class QWebSocket;

namespace HubSight::Admin {

enum class RelayErrorCode {
  InvalidConfiguration,
  ConnectionFailed,
  ProtocolError,
  Canceled,
  Unknown,
};

struct HUBSIGHT_ADMIN_EXPORT RelayError {
  RelayErrorCode code = RelayErrorCode::Unknown;
  QString message;
  int transportError = 0;
  bool retryable = false;
};

// Standard JSON WebSocket transport for the Admin relay contract. This class
// intentionally does not use Engine.IO or Socket.IO framing.
class HUBSIGHT_ADMIN_EXPORT StandardRelayClient final : public QObject {
  Q_OBJECT

public:
  enum class State {
    Disconnected,
    Connecting,
    Reconnecting,
    Connected,
  };
  Q_ENUM(State)

  explicit StandardRelayClient(QObject *parent = nullptr);
  ~StandardRelayClient() override;

  // The base URL must be an origin (or a deployment prefix) using WSS in
  // production. WS is accepted only for loopback tests.
  bool setBaseUrl(const QUrl &url);
  QUrl baseUrl() const;
  void clearConfiguration();

  // Absolute endpoint path, normally /relay/admin/v1. It never carries query
  // credentials or fragments.
  bool setPath(const QString &path);
  QString path() const;

  void setHeader(const QByteArray &name, const QByteArray &value);
  void removeHeader(const QByteArray &name);
  void clearHeaders();

  void setReconnectEnabled(bool enabled);
  bool reconnectEnabled() const;
  void setReconnectDelays(int initialDelayMs, int maximumDelayMs);
  void setMaxReconnectAttempts(int maximumAttempts);
  void setHandshakeTimeout(int timeoutMs);
  void setHeartbeatInterval(int intervalMs);

  State state() const;
  bool isConnected() const;

  void connectToServer();
  void reconnect();
  void disconnectFromServer(const QString &reason = {});

  // Sends the supplied JSON object without wrapping or changing it.
  quint64 send(const QJsonObject &message);

  // Opt-in request helper. It adds a string request_id only when the caller
  // did not provide one; responses carrying request_id, correlation_id, or id
  // are matched through requestCompleted().
  quint64 sendRequest(QJsonObject message);

signals:
  void stateChanged(HubSight::Admin::StandardRelayClient::State state);
  void connected();
  void disconnected(QString reason);
  void messageReceived(QJsonObject message);
  void requestCompleted(quint64 requestId, QJsonObject message);
  void errorOccurred(HubSight::Admin::RelayError error);
  void reconnectScheduled(int attempt, int delayMs);

private:
  QUrl websocketUrl() const;
  bool sendInternal(const QJsonObject &message);
  void startConnection(bool reconnecting);
  void handleSocketConnected();
  void handleSocketDisconnected();
  void handleTransportError(int error, const QString &message);
  void handleTextMessage(const QString &message);
  void handleBinaryMessage(const QByteArray &message);
  void handleMessage(const QByteArray &message);
  void setState(State state);
  void emitError(RelayErrorCode code, const QString &message, bool retryable,
                 int transportError = 0);
  void scheduleReconnect();
  void disconnectInternal(const QString &reason, bool reconnectAfterDisconnect);

  QWebSocket *m_socket = nullptr;
  QTimer *m_reconnectTimer = nullptr;
  QTimer *m_handshakeTimer = nullptr;
  QTimer *m_heartbeatTimer = nullptr;
  QUrl m_baseUrl;
  QString m_path = QStringLiteral("/relay/admin/v1");
  QHash<QByteArray, QByteArray> m_headers;
  QHash<QString, quint64> m_pendingRequests;
  State m_state = State::Disconnected;
  bool m_reconnectEnabled = true;
  bool m_manualDisconnect = true;
  bool m_reconnectAfterDisconnect = false;
  QString m_lastDisconnectReason;
  int m_handshakeTimeoutMs = 10000;
  int m_heartbeatIntervalMs = 25000;
  int m_reconnectInitialDelayMs = 1000;
  int m_reconnectMaximumDelayMs = 30000;
  int m_maxReconnectAttempts = -1;
  int m_reconnectAttempt = 0;
  quint64 m_nextRequestId = 1;
};

} // namespace HubSight::Admin

Q_DECLARE_METATYPE(HubSight::Admin::RelayErrorCode)
Q_DECLARE_METATYPE(HubSight::Admin::RelayError)
Q_DECLARE_METATYPE(HubSight::Admin::StandardRelayClient::State)
