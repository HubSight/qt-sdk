#include "../../include/hubsight/admin/realtime/relay_client.h"

#include <QAbstractSocket>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkRequest>
#include <QTimer>
#include <QWebSocket>

namespace HubSight::Admin {
namespace {

bool isLoopback(const QString &host) {
  return host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0 ||
         host == QStringLiteral("127.0.0.1") || host == QStringLiteral("::1");
}

QString normalizePath(QString path, bool *valid) {
  path = path.trimmed();
  if (path.isEmpty()) {
    path = QStringLiteral("/");
  }
  if (!path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char('?')) ||
      path.contains(QLatin1Char('#'))) {
    if (valid) {
      *valid = false;
    }
    return {};
  }
  while (path.size() > 1 && path.endsWith(QLatin1Char('/'))) {
    path.chop(1);
  }
  if (valid) {
    *valid = true;
  }
  return path;
}

QString joinPath(const QString &base, const QString &suffix) {
  if (base.isEmpty() || base == QStringLiteral("/")) {
    return suffix;
  }
  if (suffix == QStringLiteral("/")) {
    return base;
  }
  return base + suffix;
}

QString requestCorrelation(const QJsonObject &object) {
  for (const QString &key :
       {QStringLiteral("request_id"), QStringLiteral("correlation_id"),
        QStringLiteral("id")}) {
    const QJsonValue value = object.value(key);
    if (value.isString()) {
      return value.toString();
    }
    if (value.isDouble()) {
      return QString::number(value.toInteger());
    }
  }
  return {};
}

} // namespace

StandardRelayClient::StandardRelayClient(QObject *parent) : QObject(parent) {
  m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
  m_reconnectTimer = new QTimer(this);
  m_handshakeTimer = new QTimer(this);
  m_heartbeatTimer = new QTimer(this);
  m_reconnectTimer->setSingleShot(true);
  m_handshakeTimer->setSingleShot(true);
  m_heartbeatTimer->setInterval(m_heartbeatIntervalMs);

  connect(m_socket, &QWebSocket::connected, this,
          [this]() { handleSocketConnected(); });
  connect(m_socket, &QWebSocket::disconnected, this,
          [this]() { handleSocketDisconnected(); });
  connect(m_socket, &QWebSocket::textMessageReceived, this,
          [this](const QString &message) { handleTextMessage(message); });
  connect(m_socket, &QWebSocket::binaryMessageReceived, this,
          [this](const QByteArray &message) { handleBinaryMessage(message); });
  connect(m_socket, &QWebSocket::errorOccurred, this,
          [this](QAbstractSocket::SocketError error) {
            handleTransportError(static_cast<int>(error),
                                 m_socket->errorString());
          });
  connect(m_reconnectTimer, &QTimer::timeout, this,
          [this]() { startConnection(true); });
  connect(m_handshakeTimer, &QTimer::timeout, this, [this]() {
    emitError(RelayErrorCode::ConnectionFailed,
              QStringLiteral("The Admin relay WebSocket handshake timed out."),
              true);
    disconnectInternal(QStringLiteral("relay handshake timeout"), true);
  });
  connect(m_heartbeatTimer, &QTimer::timeout, this, [this]() {
    if (isConnected()) {
      m_socket->ping();
    }
  });

  qRegisterMetaType<RelayErrorCode>();
  qRegisterMetaType<RelayError>();
  qRegisterMetaType<StandardRelayClient::State>();
}

StandardRelayClient::~StandardRelayClient() {
  m_manualDisconnect = true;
  m_reconnectTimer->stop();
  m_handshakeTimer->stop();
  m_heartbeatTimer->stop();
  if (m_socket->state() != QAbstractSocket::UnconnectedState) {
    m_socket->abort();
  }
}

bool StandardRelayClient::setBaseUrl(const QUrl &url) {
  const QString scheme = url.scheme().toLower();
  const bool secure = scheme == QStringLiteral("wss");
  const bool loopbackWs =
      isLoopback(url.host()) && scheme == QStringLiteral("ws");
  if (!url.isValid() || url.host().isEmpty() || (!secure && !loopbackWs) ||
      !url.userInfo().isEmpty() || !url.query().isEmpty() ||
      !url.fragment().isEmpty()) {
    emitError(RelayErrorCode::InvalidConfiguration,
              QStringLiteral(
                  "Admin relay URL must use WSS; WS is allowed only "
                  "for loopback tests and URL credentials are not allowed."),
              false);
    return false;
  }
  bool validPath = false;
  const QString path = normalizePath(url.path(), &validPath);
  if (!validPath) {
    emitError(RelayErrorCode::InvalidConfiguration,
              QStringLiteral("Admin relay base URL path is invalid."), false);
    return false;
  }
  QUrl normalized = url;
  normalized.setScheme(scheme);
  normalized.setPath(path == QStringLiteral("/") ? QString() : path);
  if (normalized == m_baseUrl) {
    return true;
  }
  if (m_state != State::Disconnected) {
    disconnectFromServer(QStringLiteral("Admin relay URL changed"));
  }
  m_baseUrl = normalized;
  return true;
}

QUrl StandardRelayClient::baseUrl() const { return m_baseUrl; }

void StandardRelayClient::clearConfiguration() {
  disconnectFromServer(QStringLiteral("Admin relay configuration cleared"));
  m_baseUrl = QUrl{};
  m_headers.clear();
  m_pendingRequests.clear();
}

bool StandardRelayClient::setPath(const QString &path) {
  bool valid = false;
  const QString normalized = normalizePath(path, &valid);
  if (!valid || normalized == QStringLiteral("/")) {
    emitError(RelayErrorCode::InvalidConfiguration,
              QStringLiteral("Admin relay path must be an absolute path."),
              false);
    return false;
  }
  if (m_path == normalized) {
    return true;
  }
  if (m_state != State::Disconnected) {
    disconnectFromServer(QStringLiteral("Admin relay path changed"));
  }
  m_path = normalized;
  return true;
}

QString StandardRelayClient::path() const { return m_path; }

void StandardRelayClient::setHeader(const QByteArray &name,
                                    const QByteArray &value) {
  const QByteArray normalized = name.trimmed();
  if (normalized.isEmpty()) {
    return;
  }
  for (const QByteArray &existing : m_headers.keys()) {
    if (existing.compare(normalized, Qt::CaseInsensitive) == 0) {
      m_headers.remove(existing);
    }
  }
  if (!value.isEmpty()) {
    m_headers.insert(normalized, value);
  }
}

void StandardRelayClient::removeHeader(const QByteArray &name) {
  for (const QByteArray &existing : m_headers.keys()) {
    if (existing.compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
      m_headers.remove(existing);
    }
  }
}

void StandardRelayClient::clearHeaders() { m_headers.clear(); }

void StandardRelayClient::setReconnectEnabled(bool enabled) {
  m_reconnectEnabled = enabled;
  if (!enabled) {
    m_reconnectTimer->stop();
    m_reconnectAttempt = 0;
    m_reconnectAfterDisconnect = false;
  }
}

bool StandardRelayClient::reconnectEnabled() const {
  return m_reconnectEnabled;
}

void StandardRelayClient::setReconnectDelays(int initialDelayMs,
                                             int maximumDelayMs) {
  m_reconnectInitialDelayMs = qMax(1, initialDelayMs);
  m_reconnectMaximumDelayMs = qMax(m_reconnectInitialDelayMs, maximumDelayMs);
}

void StandardRelayClient::setMaxReconnectAttempts(int maximumAttempts) {
  m_maxReconnectAttempts = qMax(-1, maximumAttempts);
}

void StandardRelayClient::setHandshakeTimeout(int timeoutMs) {
  m_handshakeTimeoutMs = qMax(1, timeoutMs);
}

void StandardRelayClient::setHeartbeatInterval(int intervalMs) {
  m_heartbeatIntervalMs = qMax(0, intervalMs);
  m_heartbeatTimer->setInterval(m_heartbeatIntervalMs);
  if (m_heartbeatIntervalMs == 0) {
    m_heartbeatTimer->stop();
  } else if (isConnected()) {
    m_heartbeatTimer->start();
  }
}

StandardRelayClient::State StandardRelayClient::state() const {
  return m_state;
}

bool StandardRelayClient::isConnected() const {
  return m_state == State::Connected &&
         m_socket->state() == QAbstractSocket::ConnectedState;
}

void StandardRelayClient::connectToServer() {
  if (m_state != State::Disconnected) {
    return;
  }
  m_manualDisconnect = false;
  m_lastDisconnectReason.clear();
  m_reconnectAttempt = 0;
  startConnection(false);
}

void StandardRelayClient::reconnect() {
  m_reconnectTimer->stop();
  m_reconnectAttempt = 0;
  m_manualDisconnect = false;
  m_lastDisconnectReason.clear();
  if (m_socket->state() == QAbstractSocket::UnconnectedState) {
    startConnection(false);
  } else {
    m_reconnectAfterDisconnect = true;
    m_socket->close();
  }
}

void StandardRelayClient::disconnectFromServer(const QString &reason) {
  disconnectInternal(reason, false);
}

QUrl StandardRelayClient::websocketUrl() const {
  QUrl url = m_baseUrl;
  url.setPath(joinPath(url.path(), m_path));
  url.setQuery(QString());
  url.setFragment(QString());
  return url;
}

void StandardRelayClient::startConnection(bool reconnecting) {
  if (m_baseUrl.isEmpty() || m_path.isEmpty()) {
    emitError(
        RelayErrorCode::InvalidConfiguration,
        QStringLiteral("Configure the Admin relay URL before connecting."),
        false);
    setState(State::Disconnected);
    return;
  }
  if (m_socket->state() != QAbstractSocket::UnconnectedState) {
    return;
  }

  setState(reconnecting ? State::Reconnecting : State::Connecting);
  QNetworkRequest request(websocketUrl());
  for (auto it = m_headers.cbegin(); it != m_headers.cend(); ++it) {
    request.setRawHeader(it.key(), it.value());
  }
  m_manualDisconnect = false;
  m_handshakeTimer->start(m_handshakeTimeoutMs);
  m_socket->open(request);
}

void StandardRelayClient::handleSocketConnected() {
  m_handshakeTimer->stop();
  m_reconnectAttempt = 0;
  setState(State::Connected);
  if (m_heartbeatIntervalMs > 0) {
    m_heartbeatTimer->start();
  }
  emit connected();
}

void StandardRelayClient::handleSocketDisconnected() {
  m_handshakeTimer->stop();
  m_heartbeatTimer->stop();
  const bool reconnect =
      m_reconnectAfterDisconnect || (!m_manualDisconnect && m_reconnectEnabled);
  m_reconnectAfterDisconnect = false;
  const QString reason = m_lastDisconnectReason.isEmpty()
                             ? QStringLiteral("Admin relay disconnected")
                             : m_lastDisconnectReason;
  m_pendingRequests.clear();
  setState(State::Disconnected);
  emit disconnected(reason);
  if (reconnect) {
    m_manualDisconnect = false;
    scheduleReconnect();
  }
}

void StandardRelayClient::handleTransportError(int error,
                                               const QString &message) {
  emitError(RelayErrorCode::ConnectionFailed,
            message.isEmpty() ? QStringLiteral("Admin relay transport failed.")
                              : message,
            true, error);
}

void StandardRelayClient::handleTextMessage(const QString &message) {
  handleMessage(message.toUtf8());
}

void StandardRelayClient::handleBinaryMessage(const QByteArray &message) {
  handleMessage(message);
}

void StandardRelayClient::handleMessage(const QByteArray &message) {
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(message, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    emitError(RelayErrorCode::ProtocolError,
              QStringLiteral("Admin relay messages must be JSON objects."),
              false);
    return;
  }
  const QJsonObject object = document.object();
  const QString correlation = requestCorrelation(object);
  if (!correlation.isEmpty() && m_pendingRequests.contains(correlation)) {
    const quint64 requestId = m_pendingRequests.take(correlation);
    emit requestCompleted(requestId, object);
    return;
  }
  emit messageReceived(object);
}

quint64 StandardRelayClient::send(const QJsonObject &message) {
  if (!isConnected()) {
    emitError(
        RelayErrorCode::ConnectionFailed,
        QStringLiteral("Cannot send while the Admin relay is disconnected."),
        false);
    return 0;
  }
  if (!sendInternal(message)) {
    return 0;
  }
  return m_nextRequestId++;
}

quint64 StandardRelayClient::sendRequest(QJsonObject message) {
  if (!isConnected()) {
    emitError(
        RelayErrorCode::ConnectionFailed,
        QStringLiteral("Cannot send while the Admin relay is disconnected."),
        false);
    return 0;
  }
  quint64 id = m_nextRequestId++;
  if (!message.contains(QStringLiteral("request_id"))) {
    message.insert(QStringLiteral("request_id"), QString::number(id));
  }
  const QString correlation = requestCorrelation(message);
  if (correlation.isEmpty()) {
    emitError(RelayErrorCode::ProtocolError,
              QStringLiteral("Admin relay request correlation is invalid."),
              false);
    return 0;
  }
  if (!sendInternal(message)) {
    return 0;
  }
  m_pendingRequests.insert(correlation, id);
  return id;
}

bool StandardRelayClient::sendInternal(const QJsonObject &message) {
  if (message.isEmpty()) {
    emitError(RelayErrorCode::ProtocolError,
              QStringLiteral("Admin relay messages must not be empty."), false);
    return false;
  }
  m_socket->sendTextMessage(
      QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
  return true;
}

void StandardRelayClient::setState(State state) {
  if (m_state == state) {
    return;
  }
  m_state = state;
  emit stateChanged(m_state);
}

void StandardRelayClient::emitError(RelayErrorCode code, const QString &message,
                                    bool retryable, int transportError) {
  RelayError error;
  error.code = code;
  error.message = message;
  error.retryable = retryable;
  error.transportError = transportError;
  emit errorOccurred(error);
}

void StandardRelayClient::scheduleReconnect() {
  if (!m_reconnectEnabled || (m_maxReconnectAttempts >= 0 &&
                              m_reconnectAttempt >= m_maxReconnectAttempts)) {
    return;
  }
  ++m_reconnectAttempt;
  const int delay =
      qMin(m_reconnectMaximumDelayMs,
           m_reconnectInitialDelayMs * (1 << qMin(m_reconnectAttempt - 1, 15)));
  emit reconnectScheduled(m_reconnectAttempt, delay);
  m_reconnectTimer->start(delay);
  setState(State::Reconnecting);
}

void StandardRelayClient::disconnectInternal(const QString &reason,
                                             bool reconnectAfterDisconnect) {
  m_lastDisconnectReason =
      reason.isEmpty() ? QStringLiteral("client disconnected") : reason;
  m_reconnectAfterDisconnect = reconnectAfterDisconnect;
  m_manualDisconnect = !reconnectAfterDisconnect;
  m_reconnectTimer->stop();
  m_handshakeTimer->stop();
  m_heartbeatTimer->stop();
  if (m_socket->state() != QAbstractSocket::UnconnectedState) {
    m_socket->close();
  } else {
    setState(State::Disconnected);
  }
}

} // namespace HubSight::Admin
