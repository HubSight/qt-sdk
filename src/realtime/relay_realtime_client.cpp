#include "../../include/hubsight/admin/realtime/relay_realtime_client.h"

#include <QJsonArray>
#include <QJsonValue>

#include <initializer_list>
#include <utility>

namespace HubSight::Admin {
namespace {

const QStringList &allowedTopics() {
  static const QStringList topics = {
      QStringLiteral("admin_api.enabled"),
      QStringLiteral("admin_api.disabled"),
      QStringLiteral("auth.force_logout"),
      QStringLiteral("session.revoked"),
      QStringLiteral("camera.started"),
      QStringLiteral("camera.stopped"),
      QStringLiteral("camera.updated"),
      QStringLiteral("pool.status.update"),
      QStringLiteral("nvr.status.update"),
      QStringLiteral("vision.person.entered"),
      QStringLiteral("vision.person.update"),
      QStringLiteral("vision.person.left"),
      QStringLiteral("vision.log.new"),
      QStringLiteral("member.face.updated"),
      QStringLiteral("notification.new"),
      QStringLiteral("operation.progress"),
      QStringLiteral("operation.completed"),
      QStringLiteral("operation.failed"),
  };
  return topics;
}

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

QJsonValue responseValue(const QJsonObject &response, const QString &key) {
  const QJsonValue direct = response.value(key);
  if (!direct.isUndefined()) {
    return direct;
  }
  const QJsonObject data = response.value(QStringLiteral("data")).toObject();
  return data.value(key);
}

QString responseString(const QJsonObject &response,
                       std::initializer_list<const char *> keys) {
  for (const char *key : keys) {
    const QJsonValue value = responseValue(response, QString::fromLatin1(key));
    if (value.isString() && !value.toString().trimmed().isEmpty()) {
      return value.toString();
    }
  }
  return {};
}

bool responseIndicatesFailure(const QJsonObject &response,
                              bool *replayUnavailable, QString *reason) {
  if (replayUnavailable) {
    *replayUnavailable = false;
  }

  const QString status = responseValue(response, QStringLiteral("status"))
                             .toString()
                             .trimmed()
                             .toLower();
  const QString normalizedStatus = status;
  const bool unavailable =
      normalizedStatus == QStringLiteral("replay_unavailable") ||
      normalizedStatus == QStringLiteral("replay-unavailable") ||
      normalizedStatus == QStringLiteral("no_replay") ||
      normalizedStatus == QStringLiteral("unavailable") ||
      normalizedStatus == QStringLiteral("not_available");
  const QJsonValue replayAvailable =
      responseValue(response, QStringLiteral("replay_available"));
  const bool explicitlyUnavailable =
      replayAvailable.isBool() && !replayAvailable.toBool();
  if (replayUnavailable) {
    *replayUnavailable = unavailable || explicitlyUnavailable;
  }

  const QJsonValue errorValue =
      responseValue(response, QStringLiteral("error"));
  const bool hasError =
      (errorValue.isString() && !errorValue.toString().trimmed().isEmpty()) ||
      errorValue.isObject();
  const QJsonValue accepted =
      responseValue(response, QStringLiteral("accepted"));
  const QJsonValue ok = responseValue(response, QStringLiteral("ok"));
  const QJsonValue success = responseValue(response, QStringLiteral("success"));
  const bool failedStatus = status == QStringLiteral("error") ||
                            status == QStringLiteral("failed") ||
                            status == QStringLiteral("failure") ||
                            status == QStringLiteral("rejected") || unavailable;
  const bool failed =
      hasError || failedStatus || (accepted.isBool() && !accepted.toBool()) ||
      (ok.isBool() && !ok.toBool()) || (success.isBool() && !success.toBool());

  if (reason) {
    *reason = responseString(response, {"message", "reason", "error"});
    if (reason->isEmpty()) {
      *reason = failed ? QStringLiteral("The Admin relay command was rejected.")
                       : QStringLiteral("The Admin relay command completed.");
    }
  }
  return failed;
}

} // namespace

bool RelayEvent::isValid() const {
  return !eventId.trimmed().isEmpty() && schemaVersion > 0 &&
         !topic.trimmed().isEmpty() && timestamp.isValid() &&
         raw.value(QStringLiteral("data")).isObject();
}

RelayEvent RelayEvent::fromJson(const QJsonObject &json) {
  RelayEvent result;
  result.eventId = json.value(QStringLiteral("event_id")).toString().trimmed();
  result.schemaVersion = json.value(QStringLiteral("schema_version")).toInt(0);
  result.topic = json.value(QStringLiteral("topic")).toString().trimmed();
  result.timestamp = parseTimestamp(json.value(QStringLiteral("timestamp")));
  if (json.value(QStringLiteral("data")).isObject()) {
    result.data = json.value(QStringLiteral("data")).toObject();
  }
  result.raw = json;
  return result;
}

RelayRealtimeClient::RelayRealtimeClient(StandardRelayClient *transport,
                                         QObject *parent)
    : QObject(parent), m_transport(transport) {
  Q_ASSERT(m_transport);
  qRegisterMetaType<RelayEvent>();
  qRegisterMetaType<Notification>();
  qRegisterMetaType<RelayError>();

  connect(m_transport, &StandardRelayClient::connected, this,
          [this]() { handleConnected(); });
  connect(m_transport, &StandardRelayClient::disconnected, this,
          [this](const QString &) { handleDisconnected(); });
  connect(m_transport, &StandardRelayClient::requestCompleted, this,
          [this](quint64 requestId, const QJsonObject &message) {
            handleResponse(requestId, message);
          });
  connect(m_transport, &StandardRelayClient::messageReceived, this,
          [this](const QJsonObject &message) { handleMessage(message); });
  connect(m_transport, &StandardRelayClient::errorOccurred, this,
          [this](const RelayError &error) { emit errorOccurred(error); });
}

StandardRelayClient *RelayRealtimeClient::transport() const {
  return m_transport;
}

QStringList RelayRealtimeClient::topics() const {
  QStringList result = m_desiredTopics.values();
  result.sort();
  return result;
}

QString RelayRealtimeClient::lastEventId() const { return m_lastEventId; }

quint64 RelayRealtimeClient::subscribeTopic(const QString &topic) {
  return subscribeTopics(QStringList{topic});
}

quint64 RelayRealtimeClient::subscribeTopics(const QStringList &topics) {
  QStringList normalizedTopics;
  QSet<QString> seen;
  for (const QString &topic : topics) {
    QString normalized;
    QString message;
    if (!validateTopic(topic, &normalized, &message)) {
      emitInvalidTopicError(message);
      return 0;
    }
    if (!seen.contains(normalized)) {
      seen.insert(normalized);
      normalizedTopics.append(normalized);
    }
  }
  if (normalizedTopics.isEmpty()) {
    emitInvalidTopicError(
        QStringLiteral("At least one relay topic is required."));
    return 0;
  }

  QStringList topicsToSend;
  for (const QString &topic : normalizedTopics) {
    m_desiredTopics.insert(topic);
    if (m_activeTopics.contains(topic)) {
      continue;
    }

    bool pendingSubscribe = false;
    for (const PendingCommand &pending : std::as_const(m_pendingCommands)) {
      if (pending.kind == CommandKind::Subscribe &&
          pending.topics.contains(topic)) {
        pendingSubscribe = true;
        break;
      }
    }
    if (!pendingSubscribe) {
      topicsToSend.append(topic);
    }
  }

  if (topicsToSend.isEmpty() || !m_transport->isConnected()) {
    return 0;
  }
  return sendCommand(
      QJsonObject{
          {QStringLiteral("command"), QStringLiteral("subscribe")},
          {QStringLiteral("topics"), QJsonArray::fromStringList(topicsToSend)}},
      PendingCommand{CommandKind::Subscribe, topicsToSend, {}});
}

quint64 RelayRealtimeClient::unsubscribeTopic(const QString &topic) {
  QString normalized;
  QString message;
  if (!validateTopic(topic, &normalized, &message)) {
    emitInvalidTopicError(message);
    return 0;
  }
  m_desiredTopics.remove(normalized);
  if (!m_activeTopics.contains(normalized) || !m_transport->isConnected()) {
    return 0;
  }

  for (const PendingCommand &pending : std::as_const(m_pendingCommands)) {
    if (pending.kind == CommandKind::Unsubscribe &&
        pending.topic == normalized) {
      return 0;
    }
  }

  return sendCommand(
      QJsonObject{{QStringLiteral("command"), QStringLiteral("unsubscribe")},
                  {QStringLiteral("topic"), normalized}},
      PendingCommand{CommandKind::Unsubscribe, {}, normalized});
}

void RelayRealtimeClient::clearTopicSubscriptions() {
  const QStringList activeTopics = m_activeTopics.values();
  m_desiredTopics.clear();

  if (m_transport->isConnected()) {
    for (const QString &topic : activeTopics) {
      sendCommand(QJsonObject{{QStringLiteral("command"),
                               QStringLiteral("unsubscribe")},
                              {QStringLiteral("topic"), topic}},
                  PendingCommand{CommandKind::Unsubscribe, {}, topic});
    }
  }
  m_activeTopics.clear();
}

quint64 RelayRealtimeClient::requestResume() {
  if (m_lastEventId.isEmpty()) {
    RelayError error;
    error.code = RelayErrorCode::InvalidConfiguration;
    error.message = QStringLiteral(
        "Cannot request Admin relay replay before an event_id is received.");
    error.retryable = false;
    emit errorOccurred(error);
    emit replayCompleted(false, error.message);
    return 0;
  }
  return sendCommand(
      QJsonObject{{QStringLiteral("command"), QStringLiteral("resume")},
                  {QStringLiteral("last_event_id"), m_lastEventId}},
      PendingCommand{CommandKind::Resume, {}, {}});
}

quint64 RelayRealtimeClient::ping() {
  return sendCommand(
      QJsonObject{{QStringLiteral("command"), QStringLiteral("ping")}},
      PendingCommand{CommandKind::Ping, {}, {}});
}

void RelayRealtimeClient::clearLastEventId() { m_lastEventId.clear(); }

bool RelayRealtimeClient::validateTopic(const QString &topic,
                                        QString *normalized,
                                        QString *message) const {
  const QString value = topic.trimmed();
  if (value.isEmpty()) {
    if (message) {
      *message = QStringLiteral("Admin relay topic must not be empty.");
    }
    return false;
  }
  if (!allowedTopics().contains(value)) {
    if (message) {
      *message =
          QStringLiteral("Admin relay topic is not allowlisted: ") + value;
    }
    return false;
  }
  if (normalized) {
    *normalized = value;
  }
  return true;
}

quint64 RelayRealtimeClient::sendCommand(QJsonObject command,
                                         PendingCommand pending) {
  const quint64 requestId = m_transport->sendRequest(std::move(command));
  if (requestId != 0) {
    m_pendingCommands.insert(requestId, std::move(pending));
  }
  return requestId;
}

void RelayRealtimeClient::handleConnected() {
  m_activeTopics.clear();
  m_pendingCommands.clear();
  const bool reconnecting = m_reconnectPending;
  m_reconnectPending = false;
  m_hasConnected = true;
  rejoinTopics();
  if (reconnecting) {
    emit snapshotReconciliationRequired(m_lastEventId);
  }
}

void RelayRealtimeClient::handleDisconnected() {
  if (m_hasConnected) {
    m_reconnectPending = true;
  }
  m_activeTopics.clear();
  m_pendingCommands.clear();
}

void RelayRealtimeClient::handleResponse(quint64 requestId,
                                         const QJsonObject &message) {
  const auto iterator = m_pendingCommands.find(requestId);
  if (iterator == m_pendingCommands.end()) {
    return;
  }
  const PendingCommand command = iterator.value();
  m_pendingCommands.erase(iterator);

  bool replayUnavailable = false;
  QString reason;
  const bool failed =
      responseIndicatesFailure(message, &replayUnavailable, &reason);
  if (command.kind == CommandKind::Resume) {
    if (failed || replayUnavailable) {
      emit replayCompleted(false, reason);
    } else {
      bool resumed = true;
      const QJsonValue replayed =
          responseValue(message, QStringLiteral("replayed"));
      const QJsonValue resumedValue =
          responseValue(message, QStringLiteral("resumed"));
      const QJsonValue replayAvailable =
          responseValue(message, QStringLiteral("replay_available"));
      if (replayed.isBool()) {
        resumed = replayed.toBool();
      } else if (resumedValue.isBool()) {
        resumed = resumedValue.toBool();
      } else if (replayAvailable.isBool()) {
        resumed = replayAvailable.toBool();
      }
      emit replayCompleted(resumed, reason);
    }
    return;
  }

  if (failed) {
    emitCommandFailure(command, reason);
    return;
  }

  if (command.kind == CommandKind::Subscribe) {
    for (const QString &topic : command.topics) {
      if (m_desiredTopics.contains(topic)) {
        m_activeTopics.insert(topic);
        emit topicSubscribed(topic);
      } else {
        // A subscription may have been removed while its acknowledgement was
        // in flight. Bring the server back to the current desired state.
        unsubscribeTopic(topic);
      }
    }
  } else if (command.kind == CommandKind::Unsubscribe) {
    m_activeTopics.remove(command.topic);
    emit topicUnsubscribed(command.topic);
    if (m_desiredTopics.contains(command.topic)) {
      subscribeTopic(command.topic);
    }
  }
}

void RelayRealtimeClient::handleMessage(const QJsonObject &message) {
  const RelayEvent event = RelayEvent::fromJson(message);
  if (!event.isValid()) {
    RelayError error;
    error.code = RelayErrorCode::ProtocolError;
    error.message = QStringLiteral(
        "Admin relay event envelopes must contain event_id, schema_version, "
        "topic, ISO timestamp, and object data.");
    error.retryable = false;
    emit errorOccurred(error);
    return;
  }
  if (!allowedTopics().contains(event.topic)) {
    RelayError error;
    error.code = RelayErrorCode::ProtocolError;
    error.message = QStringLiteral("Admin relay sent an unauthorized topic: ") +
                    event.topic;
    error.retryable = false;
    emit errorOccurred(error);
    return;
  }
  m_lastEventId = event.eventId;
  handleEvent(event);
}

void RelayRealtimeClient::handleEvent(const RelayEvent &event) {
  emit eventReceived(event);

  if (event.topic == QStringLiteral("admin_api.enabled")) {
    emit adminApiEnabled(event);
  } else if (event.topic == QStringLiteral("admin_api.disabled")) {
    emit adminApiDisabled(event);
  } else if (event.topic == QStringLiteral("session.revoked")) {
    emit sessionRevoked(event);
  } else if (event.topic == QStringLiteral("auth.force_logout")) {
    emit forceLogout(event);
  } else if (event.topic == QStringLiteral("notification.new")) {
    const Notification notification = Notification::fromJson(event.data);
    if (notification.isValid()) {
      emit notificationReceived(notification);
    }
  } else if (event.topic.startsWith(QStringLiteral("camera."))) {
    emit cameraEvent(event);
  } else if (event.topic == QStringLiteral("pool.status.update")) {
    emit poolStatusUpdated(event);
  } else if (event.topic == QStringLiteral("nvr.status.update")) {
    emit nvrStatusUpdated(event);
  } else if (event.topic.startsWith(QStringLiteral("member."))) {
    emit memberEvent(event);
  } else if (event.topic.startsWith(QStringLiteral("vision."))) {
    emit visionEvent(event);
  } else if (event.topic.startsWith(QStringLiteral("operation."))) {
    emit operationEvent(event);
  }
}

void RelayRealtimeClient::rejoinTopics() {
  const QStringList desired = topics();
  if (desired.isEmpty() || !m_transport->isConnected()) {
    return;
  }
  sendCommand(
      QJsonObject{
          {QStringLiteral("command"), QStringLiteral("subscribe")},
          {QStringLiteral("topics"), QJsonArray::fromStringList(desired)}},
      PendingCommand{CommandKind::Subscribe, desired, {}});
}

void RelayRealtimeClient::emitInvalidTopicError(const QString &message) {
  RelayError error;
  error.code = RelayErrorCode::InvalidConfiguration;
  error.message = message;
  error.retryable = false;
  emit errorOccurred(error);
}

void RelayRealtimeClient::emitCommandFailure(const PendingCommand &command,
                                             const QString &message) {
  if (command.kind == CommandKind::Subscribe) {
    for (const QString &topic : command.topics) {
      emit topicOperationFailed(topic, QStringLiteral("subscribe"), message);
    }
  } else if (command.kind == CommandKind::Unsubscribe) {
    emit topicOperationFailed(command.topic, QStringLiteral("unsubscribe"),
                              message);
  }
}

} // namespace HubSight::Admin
