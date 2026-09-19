#include "../../include/hubsight/admin/realtime/socket_io_realtime_client.h"

#include <QJsonDocument>
#include <QJsonValue>

#include <initializer_list>

namespace HubSight::Admin {
namespace {

QDateTime parseTimestamp(const QJsonValue &value) {
  if (!value.isString()) {
    return {};
  }
  const QString text = value.toString();
  QDateTime result = QDateTime::fromString(text, Qt::ISODateWithMs);
  if (!result.isValid()) {
    result = QDateTime::fromString(text, Qt::ISODate);
  }
  return result.isValid() ? result.toUTC() : QDateTime{};
}

QDateTime firstTimestamp(const QJsonObject &object,
                         std::initializer_list<const char *> keys) {
  for (const char *key : keys) {
    const QDateTime value =
        parseTimestamp(object.value(QString::fromLatin1(key)));
    if (value.isValid()) {
      return value;
    }
  }
  return {};
}

QString firstString(const QJsonObject &object,
                    std::initializer_list<const char *> keys) {
  for (const char *key : keys) {
    const QString value = object.value(QString::fromLatin1(key)).toString();
    if (!value.isEmpty()) {
      return value;
    }
  }
  return {};
}

bool acknowledgementSucceeded(const QJsonArray &arguments, QString *message) {
  if (arguments.isEmpty() || !arguments.at(0).isObject()) {
    return true;
  }
  const QJsonObject response = arguments.at(0).toObject();
  const QString status = response.value(QStringLiteral("status")).toString();
  const bool accepted = response.value(QStringLiteral("accepted")).toBool(true);
  const bool success =
      response.value(QStringLiteral("ok"))
          .toBool(
              status.isEmpty() ||
              status.compare(QStringLiteral("ok"), Qt::CaseInsensitive) == 0 ||
              status.compare(QStringLiteral("joined"), Qt::CaseInsensitive) ==
                  0 ||
              status.compare(QStringLiteral("left"), Qt::CaseInsensitive) == 0);
  if (!accepted || !success ||
      status.compare(QStringLiteral("error"), Qt::CaseInsensitive) == 0) {
    if (message) {
      *message = firstString(response, {"message", "error"});
      if (message->isEmpty()) {
        *message = QStringLiteral("Socket.IO room operation was rejected.");
      }
    }
    return false;
  }
  return true;
}

} // namespace

bool RealtimeEvent::isValid() const { return !name.trimmed().isEmpty(); }

RealtimeEvent RealtimeEvent::fromSocketIo(const QString &event,
                                          const QJsonArray &arguments) {
  RealtimeEvent result;
  result.name = event.trimmed();
  if (arguments.isEmpty()) {
    return result;
  }

  const QJsonValue first = arguments.at(0);
  if (!first.isObject()) {
    result.data = QJsonObject{
        {QStringLiteral("arguments"), arguments},
    };
    return result;
  }

  const QJsonObject payload = first.toObject();
  result.raw = payload;
  const QString topic = firstString(payload, {"topic", "event"});
  const QJsonValue nestedData = payload.value(QStringLiteral("data"));
  if (!topic.isEmpty() && nestedData.isObject()) {
    result.name = topic;
    result.data = nestedData.toObject();
  } else {
    result.data = payload;
  }
  result.timestamp = firstTimestamp(payload, {"timestamp", "_timestamp"});
  result.sender = firstString(payload, {"sender", "_sender"});
  result.sourceSocketId =
      firstString(payload, {"socket_id", "socketId", "_from"});
  return result;
}

SocketIoRealtimeClient::SocketIoRealtimeClient(SocketIoClient *transport,
                                               QObject *parent)
    : QObject(parent), m_transport(transport) {
  Q_ASSERT(m_transport);
  qRegisterMetaType<RealtimeEvent>();
  qRegisterMetaType<Notification>();

  connect(m_transport, &SocketIoClient::connected, this,
          [this](const QString &, const QString &) { handleConnected(); });
  connect(m_transport, &SocketIoClient::disconnected, this,
          [this](const QString &) { handleDisconnected(); });
  connect(m_transport, &SocketIoClient::acknowledgementReceived, this,
          [this](quint64 acknowledgementId, const QJsonArray &arguments) {
            handleAcknowledgement(acknowledgementId, arguments);
          });
  connect(m_transport, &SocketIoClient::eventReceived, this,
          [this](const QString &event, const QJsonArray &arguments) {
            handleEvent(event, arguments);
          });
  connect(m_transport, &SocketIoClient::errorOccurred, this,
          [this](const SocketIoError &error) { emit errorOccurred(error); });
}

SocketIoClient *SocketIoRealtimeClient::transport() const {
  return m_transport;
}

QStringList SocketIoRealtimeClient::rooms() const {
  QStringList result = m_desiredRooms.values();
  result.sort();
  return result;
}

quint64 SocketIoRealtimeClient::subscribeRoom(const QString &room) {
  QString normalized;
  QString message;
  if (!validateRoom(room, &normalized, &message)) {
    emitInvalidRoomError(message);
    return 0;
  }
  m_desiredRooms.insert(normalized);
  if (m_activeRooms.contains(normalized)) {
    return 0;
  }
  return sendRoomOperation(RoomOperation::Join, normalized);
}

quint64 SocketIoRealtimeClient::unsubscribeRoom(const QString &room) {
  QString normalized;
  QString message;
  if (!validateRoom(room, &normalized, &message)) {
    emitInvalidRoomError(message);
    return 0;
  }
  m_desiredRooms.remove(normalized);
  if (!m_activeRooms.contains(normalized)) {
    return 0;
  }
  return sendRoomOperation(RoomOperation::Leave, normalized);
}

void SocketIoRealtimeClient::clearRoomSubscriptions() {
  const QStringList roomsToLeave = m_activeRooms.values();
  m_desiredRooms.clear();
  for (const QString &room : roomsToLeave) {
    sendRoomOperation(RoomOperation::Leave, room);
  }
}

bool SocketIoRealtimeClient::validateRoom(const QString &room,
                                          QString *normalized,
                                          QString *message) const {
  const QString value = room.trimmed();
  if (value.isEmpty()) {
    if (message) {
      *message = QStringLiteral("Realtime room must not be empty.");
    }
    return false;
  }
  if (value.size() > 256) {
    if (message) {
      *message = QStringLiteral("Realtime room is too long.");
    }
    return false;
  }
  for (const QChar character : value) {
    if (character.isSpace() || character.unicode() < 0x20) {
      if (message) {
        *message = QStringLiteral(
            "Realtime room must not contain whitespace or control characters.");
      }
      return false;
    }
  }
  if (normalized) {
    *normalized = value;
  }
  return true;
}

quint64 SocketIoRealtimeClient::sendRoomOperation(RoomOperation operation,
                                                  const QString &room) {
  const QString event = operation == RoomOperation::Join
                            ? QStringLiteral("join_room")
                            : QStringLiteral("leave_room");
  const quint64 acknowledgementId = m_transport->emitEvent(
      event, QJsonArray{QJsonObject{{QStringLiteral("room"), room}}}, true);
  if (acknowledgementId == 0) {
    emitRoomError(
        room, operation,
        QStringLiteral("Unable to send the Socket.IO room operation."));
    return 0;
  }
  m_pendingRoomOperations.insert(acknowledgementId, {operation, room});
  return acknowledgementId;
}

void SocketIoRealtimeClient::handleConnected() {
  m_activeRooms.clear();
  rejoinRooms();
}

void SocketIoRealtimeClient::handleDisconnected() {
  m_activeRooms.clear();
  m_pendingRoomOperations.clear();
}

void SocketIoRealtimeClient::handleAcknowledgement(
    quint64 acknowledgementId, const QJsonArray &arguments) {
  const auto pending = m_pendingRoomOperations.find(acknowledgementId);
  if (pending == m_pendingRoomOperations.end()) {
    return;
  }
  const PendingRoomOperation operation = pending.value();
  m_pendingRoomOperations.erase(pending);

  QString message;
  if (!acknowledgementSucceeded(arguments, &message)) {
    emitRoomError(operation.room, operation.operation, message);
    return;
  }

  if (operation.operation == RoomOperation::Join) {
    if (m_desiredRooms.contains(operation.room)) {
      m_activeRooms.insert(operation.room);
      emit roomJoined(operation.room);
    }
  } else {
    m_activeRooms.remove(operation.room);
    emit roomLeft(operation.room);
  }
}

void SocketIoRealtimeClient::handleEvent(const QString &event,
                                         const QJsonArray &arguments) {
  const RealtimeEvent realtimeEvent =
      RealtimeEvent::fromSocketIo(event, arguments);
  if (!realtimeEvent.isValid()) {
    return;
  }
  emit eventReceived(realtimeEvent);

  const QString name = realtimeEvent.name;
  if (name == QStringLiteral("session:revoked")) {
    emit sessionRevoked(realtimeEvent);
    return;
  }
  if (name == QStringLiteral("auth:force_logout")) {
    emit forceLogout(realtimeEvent);
    return;
  }
  if (name == QStringLiteral("notification.new")) {
    const Notification notification =
        Notification::fromJson(realtimeEvent.data);
    if (notification.isValid()) {
      emit notificationReceived(notification);
    }
    return;
  }
  if (name.startsWith(QStringLiteral("camera."))) {
    emit cameraEvent(realtimeEvent);
  } else if (name == QStringLiteral("pool.status.update")) {
    emit poolStatusUpdated(realtimeEvent);
  } else if (name == QStringLiteral("nvr.status.update")) {
    emit nvrStatusUpdated(realtimeEvent);
  } else if (name.startsWith(QStringLiteral("member."))) {
    emit memberEvent(realtimeEvent);
  } else if (name.startsWith(QStringLiteral("vision."))) {
    emit visionEvent(realtimeEvent);
  } else if (name.startsWith(QStringLiteral("operation."))) {
    emit operationEvent(realtimeEvent);
  }
}

void SocketIoRealtimeClient::emitInvalidRoomError(const QString &message) {
  SocketIoError error;
  error.code = SocketIoErrorCode::InvalidConfiguration;
  error.message = message;
  error.retryable = false;
  emit errorOccurred(error);
}

void SocketIoRealtimeClient::emitRoomError(const QString &room,
                                           RoomOperation operation,
                                           const QString &message) {
  emit roomOperationFailed(room,
                           operation == RoomOperation::Join
                               ? QStringLiteral("join")
                               : QStringLiteral("leave"),
                           message);
}

void SocketIoRealtimeClient::rejoinRooms() {
  const QStringList desired = rooms();
  for (const QString &room : desired) {
    sendRoomOperation(RoomOperation::Join, room);
  }
}

} // namespace HubSight::Admin
