#include "../../include/hubsight/admin/realtime/socket_io_client.h"

#include <QAbstractSocket>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>
#include <QWebSocket>

#include <limits>

namespace HubSight::Admin {
namespace {

constexpr int kEngineIoVersion = 4;
constexpr int kDefaultPingIntervalMs = 25000;
constexpr int kDefaultPingTimeoutMs = 20000;

bool isSupportedScheme(const QString &scheme) {
  return scheme == QStringLiteral("http") ||
         scheme == QStringLiteral("https") || scheme == QStringLiteral("ws") ||
         scheme == QStringLiteral("wss");
}

QString normalizedPath(QString path, bool *valid) {
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
  if (path == QStringLiteral("/")) {
    path.clear();
  }
  if (valid) {
    *valid = true;
  }
  return path;
}

QString joinPath(const QString &base, const QString &suffix) {
  if (base.isEmpty()) {
    return suffix;
  }
  return base + suffix;
}

} // namespace

SocketIoClient::SocketIoClient(QObject *parent) : QObject(parent) {
  m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
  m_reconnectTimer = new QTimer(this);
  m_handshakeTimer = new QTimer(this);
  m_heartbeatTimer = new QTimer(this);
  m_reconnectTimer->setSingleShot(true);
  m_handshakeTimer->setSingleShot(true);
  m_heartbeatTimer->setSingleShot(true);

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
  connect(m_handshakeTimer, &QTimer::timeout, this,
          [this]() { handleHandshakeTimeout(); });
  connect(m_heartbeatTimer, &QTimer::timeout, this,
          [this]() { handleHeartbeatTimeout(); });

  qRegisterMetaType<SocketIoErrorCode>();
  qRegisterMetaType<SocketIoError>();
  qRegisterMetaType<SocketIoClient::State>();
}

SocketIoClient::~SocketIoClient() {
  m_manualDisconnect = true;
  m_reconnectTimer->stop();
  m_handshakeTimer->stop();
  m_heartbeatTimer->stop();
  if (m_socket->state() != QAbstractSocket::UnconnectedState) {
    m_socket->abort();
  }
}

bool SocketIoClient::setBaseUrl(const QUrl &url) {
  const QString inputScheme = url.scheme().toLower();
  const bool loopbackHost = url.host().compare(QStringLiteral("localhost"),
                                               Qt::CaseInsensitive) == 0 ||
                            url.host() == QStringLiteral("127.0.0.1") ||
                            url.host() == QStringLiteral("::1");
  const bool insecureTransport = inputScheme == QStringLiteral("http") ||
                                 inputScheme == QStringLiteral("ws");
  if (!url.isValid() || url.host().isEmpty() ||
      !isSupportedScheme(inputScheme) || (insecureTransport && !loopbackHost) ||
      !url.userInfo().isEmpty() || !url.query().isEmpty() ||
      !url.fragment().isEmpty()) {
    emitError(SocketIoErrorCode::InvalidConfiguration,
              QStringLiteral(
                  "Socket.IO base URL must use HTTPS/WSS; HTTP/WS are "
                  "allowed only for loopback tests, and URL credentials are "
                  "not allowed."),
              false);
    return false;
  }

  QUrl normalized = url;
  const QString scheme = normalized.scheme().toLower();
  normalized.setScheme(scheme == QStringLiteral("http") ? QStringLiteral("ws")
                       : scheme == QStringLiteral("https")
                           ? QStringLiteral("wss")
                           : scheme);
  bool validPath = false;
  const QString path = normalizedPath(normalized.path(), &validPath);
  if (!validPath) {
    emitError(SocketIoErrorCode::InvalidConfiguration,
              QStringLiteral("Socket.IO base URL path is invalid."), false);
    return false;
  }
  normalized.setPath(path);

  if (m_baseUrl == normalized) {
    return true;
  }
  if (m_state != State::Disconnected) {
    disconnectFromServer(QStringLiteral("Socket.IO base URL changed"));
  }
  m_baseUrl = normalized;
  return true;
}

QUrl SocketIoClient::baseUrl() const { return m_baseUrl; }

void SocketIoClient::clearConfiguration() {
  disconnectFromServer(QStringLiteral("Socket.IO configuration cleared"));
  m_baseUrl = QUrl{};
  m_path = QStringLiteral("/socket.io/");
  m_namespace = QStringLiteral("/");
  m_auth = {};
  m_headers.clear();
}

bool SocketIoClient::setPath(const QString &path) {
  bool valid = false;
  const QString normalized = normalizedPath(path, &valid);
  if (!valid || normalized.isEmpty()) {
    emitError(SocketIoErrorCode::InvalidConfiguration,
              QStringLiteral("Socket.IO path must be an absolute path without "
                             "query or fragment."),
              false);
    return false;
  }
  const QString withTrailingSlash = normalized + QLatin1Char('/');
  if (m_path == withTrailingSlash) {
    return true;
  }
  if (m_state != State::Disconnected) {
    disconnectFromServer(QStringLiteral("Socket.IO path changed"));
  }
  m_path = withTrailingSlash;
  return true;
}

QString SocketIoClient::path() const { return m_path; }

bool SocketIoClient::setNamespace(const QString &namespaceName) {
  QString normalized = namespaceName.trimmed();
  if (normalized.isEmpty()) {
    normalized = QStringLiteral("/");
  }
  while (normalized.size() > 1 && normalized.endsWith(QLatin1Char('/'))) {
    normalized.chop(1);
  }
  if (!normalized.startsWith(QLatin1Char('/')) ||
      normalized.contains(QLatin1Char(',')) ||
      normalized.contains(QChar::Space)) {
    emitError(SocketIoErrorCode::InvalidConfiguration,
              QStringLiteral("Socket.IO namespace is invalid."), false);
    return false;
  }
  if (m_namespace == normalized) {
    return true;
  }
  if (m_state != State::Disconnected) {
    disconnectFromServer(QStringLiteral("Socket.IO namespace changed"));
  }
  m_namespace = normalized;
  return true;
}

QString SocketIoClient::namespaceName() const { return m_namespace; }

void SocketIoClient::setAuth(const QJsonObject &auth) { m_auth = auth; }

QJsonObject SocketIoClient::auth() const { return m_auth; }

void SocketIoClient::setHeader(const QByteArray &name,
                               const QByteArray &value) {
  const QByteArray normalizedName = name.trimmed();
  if (normalizedName.isEmpty()) {
    return;
  }
  const auto keys = m_headers.keys();
  for (const QByteArray &key : keys) {
    if (key.compare(normalizedName, Qt::CaseInsensitive) == 0) {
      m_headers.remove(key);
    }
  }
  if (!value.isEmpty()) {
    m_headers.insert(normalizedName, value);
  }
}

void SocketIoClient::removeHeader(const QByteArray &name) {
  const QByteArray normalizedName = name.trimmed();
  const auto keys = m_headers.keys();
  for (const QByteArray &key : keys) {
    if (key.compare(normalizedName, Qt::CaseInsensitive) == 0) {
      m_headers.remove(key);
    }
  }
}

void SocketIoClient::clearHeaders() { m_headers.clear(); }

void SocketIoClient::setReconnectEnabled(bool enabled) {
  m_reconnectEnabled = enabled;
  if (!enabled) {
    const bool wasReconnecting = m_state == State::Reconnecting;
    m_reconnectTimer->stop();
    m_reconnectAttempt = 0;
    m_reconnectAfterDisconnect = false;
    if (wasReconnecting) {
      m_manualDisconnect = true;
      setState(State::Disconnected);
    }
  }
}

bool SocketIoClient::reconnectEnabled() const { return m_reconnectEnabled; }

void SocketIoClient::setReconnectDelays(int initialDelayMs,
                                        int maximumDelayMs) {
  m_reconnectInitialDelayMs = qMax(1, initialDelayMs);
  m_reconnectMaximumDelayMs = qMax(m_reconnectInitialDelayMs, maximumDelayMs);
}

void SocketIoClient::setMaxReconnectAttempts(int maximumAttempts) {
  m_maxReconnectAttempts = qMax(-1, maximumAttempts);
}

void SocketIoClient::setHandshakeTimeout(int timeoutMs) {
  m_handshakeTimeoutMs = qMax(1, timeoutMs);
}

SocketIoClient::State SocketIoClient::state() const { return m_state; }

bool SocketIoClient::isConnected() const {
  return m_state == State::Connected && m_namespaceConnected;
}

QString SocketIoClient::sessionId() const { return m_sessionId; }

void SocketIoClient::connectToServer() {
  if (m_state != State::Disconnected) {
    return;
  }
  m_manualDisconnect = false;
  m_reconnectAttempt = 0;
  m_lastDisconnectReason.clear();
  startConnection(false);
}

void SocketIoClient::reconnect() {
  m_reconnectTimer->stop();
  m_reconnectAttempt = 0;
  if ((m_state == State::Disconnected || m_state == State::Reconnecting) &&
      m_socket->state() == QAbstractSocket::UnconnectedState) {
    m_reconnectAfterDisconnect = false;
    m_manualDisconnect = false;
    startConnection(false);
    return;
  }
  disconnectInternal(QStringLiteral("client reconnecting"), true);
}

void SocketIoClient::disconnectFromServer(const QString &reason) {
  disconnectInternal(reason, false);
}

void SocketIoClient::disconnectInternal(const QString &reason,
                                        bool reconnectAfterDisconnect) {
  const bool wasActive = m_state != State::Disconnected ||
                         m_socket->state() != QAbstractSocket::UnconnectedState;
  const bool namespaceWasConnected = m_namespaceConnected;
  if (namespaceWasConnected) {
    sendText(QByteArrayLiteral("41") + namespacePrefix());
  }
  m_reconnectAfterDisconnect = reconnectAfterDisconnect;
  m_manualDisconnect = true;
  m_reconnectTimer->stop();
  m_handshakeTimer->stop();
  m_heartbeatTimer->stop();
  m_engineOpened = false;
  m_namespaceConnected = false;
  m_sessionId.clear();
  m_pendingAcknowledgements.clear();
  m_requestedAcknowledgements.clear();
  m_lastDisconnectReason =
      reason.isEmpty() ? QStringLiteral("client disconnected") : reason;
  if (m_socket->state() != QAbstractSocket::UnconnectedState) {
    m_socket->close();
  }
  setState(State::Disconnected);
  if (wasActive) {
    emit disconnected(m_lastDisconnectReason);
  }
  if (m_reconnectAfterDisconnect &&
      m_socket->state() == QAbstractSocket::UnconnectedState) {
    m_reconnectAfterDisconnect = false;
    m_manualDisconnect = false;
    startConnection(false);
  }
}

quint64 SocketIoClient::emitEvent(const QString &event,
                                  const QJsonArray &arguments,
                                  bool requestAcknowledgement) {
  if (!isConnected()) {
    emitError(
        SocketIoErrorCode::ConnectionFailed,
        QStringLiteral("Cannot emit a Socket.IO event while disconnected."),
        false);
    return 0;
  }
  const QString eventName = event.trimmed();
  if (eventName.isEmpty()) {
    emitError(SocketIoErrorCode::InvalidConfiguration,
              QStringLiteral("Socket.IO event name must not be empty."), false);
    return 0;
  }

  QJsonArray payload;
  payload.append(eventName);
  for (const QJsonValue &argument : arguments) {
    payload.append(argument);
  }

  quint64 acknowledgementId = 0;
  QByteArray packet = QByteArrayLiteral("42") + namespacePrefix();
  if (requestAcknowledgement) {
    acknowledgementId = m_nextAcknowledgementId++;
    if (m_nextAcknowledgementId == 0) {
      m_nextAcknowledgementId = 1;
    }
    m_pendingAcknowledgements.insert(acknowledgementId);
    packet += QByteArray::number(acknowledgementId);
  }
  packet += QJsonDocument(payload).toJson(QJsonDocument::Compact);
  if (!sendText(packet)) {
    if (acknowledgementId != 0) {
      m_pendingAcknowledgements.remove(acknowledgementId);
    }
    return 0;
  }
  return acknowledgementId;
}

bool SocketIoClient::acknowledge(quint64 acknowledgementId,
                                 const QJsonArray &arguments) {
  if (!isConnected() ||
      !m_requestedAcknowledgements.contains(acknowledgementId)) {
    emitError(SocketIoErrorCode::ProtocolError,
              QStringLiteral("Socket.IO acknowledgement request is invalid or "
                             "no longer pending."),
              false);
    return false;
  }

  QByteArray packet = QByteArrayLiteral("43") + namespacePrefix() +
                      QByteArray::number(acknowledgementId);
  if (!arguments.isEmpty()) {
    packet += QJsonDocument(arguments).toJson(QJsonDocument::Compact);
  }
  if (!sendText(packet)) {
    return false;
  }
  m_requestedAcknowledgements.remove(acknowledgementId);
  return true;
}

QUrl SocketIoClient::websocketUrl() const {
  QUrl url = m_baseUrl;
  url.setPath(joinPath(url.path(), m_path));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("EIO"), QString::number(kEngineIoVersion));
  query.addQueryItem(QStringLiteral("transport"), QStringLiteral("websocket"));
  url.setQuery(query);
  return url;
}

QByteArray SocketIoClient::namespacePrefix() const {
  return m_namespace == QStringLiteral("/")
             ? QByteArray{}
             : m_namespace.toUtf8() + QByteArrayLiteral(",");
}

QByteArray SocketIoClient::namespaceConnectPacket() const {
  QByteArray packet = QByteArrayLiteral("40") + namespacePrefix();
  if (!m_auth.isEmpty()) {
    packet += QJsonDocument(m_auth).toJson(QJsonDocument::Compact);
  }
  return packet;
}

void SocketIoClient::startConnection(bool reconnecting) {
  if (reconnecting && (m_manualDisconnect || !m_reconnectEnabled ||
                       (m_maxReconnectAttempts >= 0 &&
                        m_reconnectAttempt > m_maxReconnectAttempts))) {
    m_reconnectTimer->stop();
    setState(State::Disconnected);
    return;
  }
  if (m_baseUrl.isEmpty()) {
    m_manualDisconnect = true;
    setState(State::Disconnected);
    emitError(
        SocketIoErrorCode::InvalidConfiguration,
        QStringLiteral("Configure a Socket.IO base URL before connecting."),
        false);
    return;
  }

  m_manualDisconnect = false;
  m_engineOpened = false;
  m_namespaceConnected = false;
  m_sessionId.clear();
  m_pendingAcknowledgements.clear();
  m_requestedAcknowledgements.clear();
  m_lastDisconnectReason.clear();
  m_transportErrorReported = false;
  if (m_socket->state() != QAbstractSocket::UnconnectedState) {
    m_socket->abort();
  }

  setState(reconnecting ? State::Reconnecting : State::Connecting);
  m_handshakeTimer->start(m_handshakeTimeoutMs);

  QNetworkRequest request(websocketUrl());
  for (auto it = m_headers.cbegin(); it != m_headers.cend(); ++it) {
    request.setRawHeader(it.key(), it.value());
  }
  m_socket->open(request);
}

void SocketIoClient::handleSocketConnected() {
  m_lastDisconnectReason.clear();
  m_transportErrorReported = false;
}

void SocketIoClient::handleSocketDisconnected() {
  m_handshakeTimer->stop();
  m_heartbeatTimer->stop();
  m_engineOpened = false;
  m_namespaceConnected = false;
  m_sessionId.clear();
  m_pendingAcknowledgements.clear();
  m_requestedAcknowledgements.clear();

  const QString reason =
      m_lastDisconnectReason.isEmpty()
          ? QStringLiteral("Socket.IO transport disconnected")
          : m_lastDisconnectReason;
  if (m_reconnectAfterDisconnect) {
    m_reconnectAfterDisconnect = false;
    m_manualDisconnect = false;
    startConnection(false);
    return;
  }
  const bool shouldReconnect = !m_manualDisconnect && m_reconnectEnabled;
  if (m_state != State::Disconnected) {
    setState(State::Disconnected);
    emit disconnected(reason);
  }
  if (shouldReconnect) {
    scheduleReconnect();
  }
}

void SocketIoClient::handleTransportError(int error, const QString &message) {
  if (m_manualDisconnect || m_transportErrorReported) {
    return;
  }
  m_transportErrorReported = true;
  m_lastDisconnectReason =
      message.isEmpty() ? QStringLiteral("Socket.IO WebSocket transport error")
                        : message;
  emitError(SocketIoErrorCode::ConnectionFailed, m_lastDisconnectReason, true,
            error);
}

void SocketIoClient::handleTextMessage(const QString &message) {
  const QByteArray packet = message.toUtf8();
  if (packet.isEmpty()) {
    emitError(SocketIoErrorCode::ProtocolError,
              QStringLiteral("Received an empty Engine.IO packet."), true);
    closeTransport(QStringLiteral("empty Engine.IO packet"));
    return;
  }

  switch (packet.at(0)) {
  case '0':
    handleEngineOpen(packet.mid(1));
    break;
  case '1':
    closeTransport(QStringLiteral("Engine.IO server closed the connection"));
    break;
  case '2':
    if (!m_engineOpened) {
      emitError(SocketIoErrorCode::ProtocolError,
                QStringLiteral("Received an Engine.IO ping before open."),
                true);
      closeTransport(QStringLiteral("invalid Engine.IO ping"));
      return;
    }
    sendEnginePong();
    restartHeartbeatTimer();
    break;
  case '3':
    if (m_engineOpened) {
      restartHeartbeatTimer();
    }
    break;
  case '4':
    handleEngineMessage(packet.mid(1));
    break;
  case '6':
    break;
  default:
    emitError(SocketIoErrorCode::ProtocolError,
              QStringLiteral("Received an unknown Engine.IO packet type."),
              true);
    closeTransport(QStringLiteral("unknown Engine.IO packet"));
    break;
  }
}

void SocketIoClient::handleBinaryMessage(const QByteArray &message) {
  Q_UNUSED(message)
  emitError(SocketIoErrorCode::Unsupported,
            QStringLiteral("Binary Socket.IO packets are not implemented in "
                           "the base client yet."),
            false);
  disconnectFromServer(QStringLiteral("binary Socket.IO packet unsupported"));
}

void SocketIoClient::handleEngineOpen(const QByteArray &payload) {
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
  const QJsonObject object =
      document.isObject() ? document.object() : QJsonObject{};
  const QString sessionId = object.value(QStringLiteral("sid")).toString();
  if (sessionId.isEmpty()) {
    emitError(
        SocketIoErrorCode::HandshakeFailed,
        QStringLiteral("Engine.IO open packet did not contain a session id."),
        true);
    closeTransport(QStringLiteral("invalid Engine.IO open packet"));
    return;
  }

  m_pingIntervalMs = qMax(1, object.value(QStringLiteral("pingInterval"))
                                 .toInt(kDefaultPingIntervalMs));
  m_pingTimeoutMs = qMax(
      1,
      object.value(QStringLiteral("pingTimeout")).toInt(kDefaultPingTimeoutMs));
  m_engineOpened = true;
  restartHeartbeatTimer();
  if (!sendText(namespaceConnectPacket())) {
    closeTransport(QStringLiteral("failed to send Socket.IO CONNECT packet"));
  }
}

void SocketIoClient::handleEngineMessage(const QByteArray &payload) {
  if (!m_engineOpened || payload.isEmpty()) {
    emitError(SocketIoErrorCode::ProtocolError,
              QStringLiteral("Received an invalid Engine.IO message packet."),
              true);
    closeTransport(QStringLiteral("invalid Engine.IO message packet"));
    return;
  }
  handleSocketPacket(payload);
}

void SocketIoClient::handleSocketPacket(const QByteArray &payload) {
  ParsedPacket packet;
  QString parseError;
  if (!parseSocketPacket(payload, &packet, &parseError)) {
    emitError(SocketIoErrorCode::ProtocolError, parseError, true);
    closeTransport(QStringLiteral("invalid Socket.IO packet"));
    return;
  }

  switch (packet.type) {
  case 0:
    handleNamespaceConnect(packet);
    break;
  case 1:
    handleNamespaceDisconnect(packet);
    break;
  case 2:
    handleSocketEvent(packet);
    break;
  case 3:
    handleSocketAcknowledgement(packet);
    break;
  case 4:
    handleSocketError(packet);
    break;
  case 5:
  case 6:
    emitError(SocketIoErrorCode::Unsupported,
              QStringLiteral("Binary Socket.IO packets are not implemented in "
                             "the base client yet."),
              false);
    disconnectFromServer(QStringLiteral("binary Socket.IO packet unsupported"));
    break;
  default:
    emitError(SocketIoErrorCode::ProtocolError,
              QStringLiteral("Received an unknown Socket.IO packet type."),
              true);
    closeTransport(QStringLiteral("unknown Socket.IO packet"));
    break;
  }
}

bool SocketIoClient::parseSocketPacket(const QByteArray &payload,
                                       ParsedPacket *packet,
                                       QString *error) const {
  if (!packet || payload.isEmpty() || payload.at(0) < '0' ||
      payload.at(0) > '6') {
    if (error) {
      *error = QStringLiteral("Socket.IO packet has an invalid type.");
    }
    return false;
  }

  packet->type = payload.at(0) - '0';
  int position = 1;
  if (position < payload.size() && payload.at(position) == '/') {
    const int comma = payload.indexOf(',', position);
    if (comma < 0) {
      packet->namespaceName = QString::fromUtf8(payload.mid(position));
      position = payload.size();
    } else {
      packet->namespaceName =
          QString::fromUtf8(payload.mid(position, comma - position));
      position = comma + 1;
    }
  }

  if (packet->type == 2 || packet->type == 3) {
    const int acknowledgementStart = position;
    while (position < payload.size() && payload.at(position) >= '0' &&
           payload.at(position) <= '9') {
      ++position;
    }
    if (position > acknowledgementStart) {
      bool converted = false;
      const quint64 acknowledgementId =
          QString::fromLatin1(payload.mid(acknowledgementStart,
                                          position - acknowledgementStart))
              .toULongLong(&converted);
      if (!converted) {
        if (error) {
          *error = QStringLiteral("Socket.IO acknowledgement id is invalid.");
        }
        return false;
      }
      packet->acknowledgementId = acknowledgementId;
      packet->hasAcknowledgementId = true;
    }
  }

  packet->payload = payload.mid(position);
  return true;
}

void SocketIoClient::handleNamespaceConnect(const ParsedPacket &packet) {
  if (packet.namespaceName != m_namespace) {
    emitError(SocketIoErrorCode::ProtocolError,
              QStringLiteral("Socket.IO connected an unexpected namespace."),
              false);
    disconnectFromServer(QStringLiteral("unexpected Socket.IO namespace"));
    return;
  }
  if (m_namespaceConnected) {
    return;
  }

  if (packet.payload.isEmpty()) {
    emitError(SocketIoErrorCode::HandshakeFailed,
              QStringLiteral("Socket.IO CONNECT packet did not contain a "
                             "namespace session payload."),
              false);
    disconnectFromServer(QStringLiteral("missing Socket.IO namespace session"));
    return;
  }

  QJsonParseError parseError;
  const QJsonDocument document =
      QJsonDocument::fromJson(packet.payload, &parseError);
  if (!document.isObject()) {
    emitError(SocketIoErrorCode::HandshakeFailed,
              QStringLiteral("Socket.IO CONNECT payload must be an object."),
              false);
    disconnectFromServer(QStringLiteral("invalid Socket.IO CONNECT payload"));
    return;
  }
  m_sessionId = document.object().value(QStringLiteral("sid")).toString();
  if (m_sessionId.isEmpty()) {
    emitError(SocketIoErrorCode::HandshakeFailed,
              QStringLiteral("Socket.IO CONNECT payload did not contain a "
                             "namespace session id."),
              false);
    disconnectFromServer(QStringLiteral("missing Socket.IO namespace session"));
    return;
  }
  m_namespaceConnected = true;
  m_handshakeTimer->stop();
  m_reconnectAttempt = 0;
  setState(State::Connected);
  emit connected(m_namespace, m_sessionId);
}

void SocketIoClient::handleNamespaceDisconnect(const ParsedPacket &packet) {
  if (packet.namespaceName != m_namespace) {
    return;
  }
  m_namespaceConnected = false;
  disconnectFromServer(QStringLiteral("Socket.IO namespace disconnected"));
}

void SocketIoClient::handleSocketEvent(const ParsedPacket &packet) {
  if (packet.namespaceName != m_namespace) {
    return;
  }
  QJsonParseError parseError;
  const QJsonDocument document =
      QJsonDocument::fromJson(packet.payload, &parseError);
  if (!document.isArray() || document.array().isEmpty() ||
      !document.array().at(0).isString()) {
    emitError(SocketIoErrorCode::ProtocolError,
              QStringLiteral("Socket.IO event payload must start with an event "
                             "name."),
              true);
    closeTransport(QStringLiteral("invalid Socket.IO event payload"));
    return;
  }

  const QJsonArray values = document.array();
  const QString event = values.at(0).toString();
  QJsonArray arguments;
  for (int index = 1; index < values.size(); ++index) {
    arguments.append(values.at(index));
  }
  if (packet.hasAcknowledgementId) {
    m_requestedAcknowledgements.insert(packet.acknowledgementId);
  }
  emit eventReceived(event, arguments);
  if (packet.hasAcknowledgementId) {
    emit eventAcknowledgementRequested(packet.acknowledgementId, event,
                                       arguments);
  }
}

void SocketIoClient::handleSocketAcknowledgement(const ParsedPacket &packet) {
  if (packet.namespaceName != m_namespace || !packet.hasAcknowledgementId) {
    emitError(SocketIoErrorCode::ProtocolError,
              QStringLiteral("Socket.IO acknowledgement is missing its id."),
              true);
    return;
  }
  if (!m_pendingAcknowledgements.remove(packet.acknowledgementId)) {
    emitError(
        SocketIoErrorCode::ProtocolError,
        QStringLiteral("Received an unknown Socket.IO acknowledgement id."),
        false);
    return;
  }

  QJsonArray arguments;
  if (!packet.payload.isEmpty()) {
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(packet.payload, &parseError);
    if (!document.isArray()) {
      emitError(SocketIoErrorCode::ProtocolError,
                QStringLiteral("Socket.IO acknowledgement payload must be an "
                               "array."),
                true);
      return;
    }
    arguments = document.array();
  }
  emit acknowledgementReceived(packet.acknowledgementId, arguments);
}

void SocketIoClient::handleSocketError(const ParsedPacket &packet) {
  QString message = QStringLiteral("Socket.IO namespace connection failed.");
  if (!packet.payload.isEmpty()) {
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(packet.payload, &parseError);
    if (document.isObject()) {
      const QJsonObject object = document.object();
      message = object.value(QStringLiteral("message")).toString(message);
    } else if (document.isArray() && !document.array().isEmpty()) {
      message = document.array().at(0).toString(message);
    } else if (QString::fromUtf8(packet.payload).size() > 0) {
      message = QString::fromUtf8(packet.payload);
    }
  }
  emitError(SocketIoErrorCode::ConnectionFailed, message, false);
  m_namespaceConnected = false;
  disconnectFromServer(message);
}

void SocketIoClient::sendEnginePong() {
  if (!sendText(QByteArrayLiteral("3"))) {
    closeTransport(QStringLiteral("failed to send Engine.IO pong"));
  }
}

bool SocketIoClient::sendText(const QByteArray &message) {
  if (m_socket->state() != QAbstractSocket::ConnectedState) {
    emitError(SocketIoErrorCode::ConnectionFailed,
              QStringLiteral("Socket.IO WebSocket is not connected."), true);
    return false;
  }
  return m_socket->sendTextMessage(QString::fromUtf8(message)) >= 0;
}

void SocketIoClient::restartHeartbeatTimer() {
  const qint64 interval = static_cast<qint64>(m_pingIntervalMs) +
                          static_cast<qint64>(m_pingTimeoutMs);
  const int boundedInterval =
      static_cast<int>(qMin<qint64>(interval, std::numeric_limits<int>::max()));
  m_heartbeatTimer->start(qMax(1, boundedInterval));
}

void SocketIoClient::handleHandshakeTimeout() {
  if (m_namespaceConnected || m_manualDisconnect) {
    return;
  }
  emitError(SocketIoErrorCode::Timeout,
            QStringLiteral("Socket.IO handshake timed out."), true);
  closeTransport(QStringLiteral("Socket.IO handshake timed out"));
}

void SocketIoClient::handleHeartbeatTimeout() {
  if (!m_engineOpened || m_manualDisconnect) {
    return;
  }
  emitError(SocketIoErrorCode::Timeout,
            QStringLiteral("Engine.IO heartbeat timed out."), true);
  closeTransport(QStringLiteral("Engine.IO heartbeat timed out"));
}

void SocketIoClient::scheduleReconnect() {
  if (m_manualDisconnect || !m_reconnectEnabled ||
      (m_maxReconnectAttempts >= 0 &&
       m_reconnectAttempt >= m_maxReconnectAttempts)) {
    return;
  }

  ++m_reconnectAttempt;
  qint64 delay = m_reconnectInitialDelayMs;
  for (int attempt = 1; attempt < m_reconnectAttempt; ++attempt) {
    delay = qMin<qint64>(delay * 2, m_reconnectMaximumDelayMs);
  }
  const int boundedDelay =
      static_cast<int>(qMin<qint64>(delay, std::numeric_limits<int>::max()));
  setState(State::Reconnecting);
  emit reconnectScheduled(m_reconnectAttempt, boundedDelay);
  m_reconnectTimer->start(qMax(1, boundedDelay));
}

void SocketIoClient::setState(State state) {
  if (m_state == state) {
    return;
  }
  m_state = state;
  emit stateChanged(m_state);
}

void SocketIoClient::emitError(SocketIoErrorCode code, const QString &message,
                               bool retryable, int transportError) {
  SocketIoError error;
  error.code = code;
  error.message = message;
  error.retryable = retryable;
  error.transportError = transportError;
  emit errorOccurred(error);
}

void SocketIoClient::closeTransport(const QString &reason) {
  if (!reason.isEmpty()) {
    m_lastDisconnectReason = reason;
  }
  if (m_socket->state() != QAbstractSocket::UnconnectedState) {
    m_socket->close();
  } else if (!m_manualDisconnect) {
    handleSocketDisconnected();
  }
}

} // namespace HubSight::Admin
