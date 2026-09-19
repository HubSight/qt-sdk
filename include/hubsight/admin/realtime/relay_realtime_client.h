#pragma once

#include "../admin_export.h"
#include "../resources/notification_types.h"
#include "relay_client.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QStringList>

namespace HubSight::Admin {

struct HUBSIGHT_ADMIN_EXPORT RelayEvent {
  QString eventId;
  int schemaVersion = 0;
  QString topic;
  QDateTime timestamp;
  QJsonObject data;
  QJsonObject raw;

  bool isValid() const;
  static RelayEvent fromJson(const QJsonObject &json);
};

// Typed domain layer for the normative Standard JSON Admin relay. This class
// deliberately exposes allowlisted topics rather than generic rooms or client
// broadcast/event-publishing APIs.
class HUBSIGHT_ADMIN_EXPORT RelayRealtimeClient final : public QObject {
  Q_OBJECT

public:
  explicit RelayRealtimeClient(StandardRelayClient *transport,
                               QObject *parent = nullptr);

  StandardRelayClient *transport() const;
  QStringList topics() const;
  QString lastEventId() const;

  quint64 subscribeTopic(const QString &topic);
  quint64 subscribeTopics(const QStringList &topics);
  quint64 unsubscribeTopic(const QString &topic);
  void clearTopicSubscriptions();

  // The application must reconcile its REST snapshot after
  // snapshotReconciliationRequired() and then call this method. Replay is
  // best-effort; replayCompleted(false, ...) is not a fatal transport error.
  quint64 requestResume();
  quint64 ping();

  void clearLastEventId();

signals:
  void topicSubscribed(QString topic);
  void topicUnsubscribed(QString topic);
  void topicOperationFailed(QString topic, QString operation, QString message);

  void eventReceived(HubSight::Admin::RelayEvent event);
  void cameraEvent(HubSight::Admin::RelayEvent event);
  void poolStatusUpdated(HubSight::Admin::RelayEvent event);
  void nvrStatusUpdated(HubSight::Admin::RelayEvent event);
  void memberEvent(HubSight::Admin::RelayEvent event);
  void visionEvent(HubSight::Admin::RelayEvent event);
  void operationEvent(HubSight::Admin::RelayEvent event);

  void notificationReceived(HubSight::Admin::Notification notification);

  void adminApiEnabled(HubSight::Admin::RelayEvent event);
  void adminApiDisabled(HubSight::Admin::RelayEvent event);
  void sessionRevoked(HubSight::Admin::RelayEvent event);
  void forceLogout(HubSight::Admin::RelayEvent event);

  void snapshotReconciliationRequired(QString lastEventId);
  void replayCompleted(bool resumed, QString reason);

  void errorOccurred(HubSight::Admin::RelayError error);

private:
  enum class CommandKind {
    Subscribe,
    Unsubscribe,
    Resume,
    Ping,
  };

  struct PendingCommand {
    CommandKind kind = CommandKind::Ping;
    QStringList topics;
    QString topic;
  };

  bool validateTopic(const QString &topic, QString *normalized,
                     QString *message) const;
  quint64 sendCommand(QJsonObject command, PendingCommand pending);
  void handleConnected();
  void handleDisconnected();
  void handleResponse(quint64 requestId, const QJsonObject &message);
  void handleMessage(const QJsonObject &message);
  void handleEvent(const RelayEvent &event);
  void rejoinTopics();
  void emitInvalidTopicError(const QString &message);
  void emitCommandFailure(const PendingCommand &command,
                          const QString &message);

  StandardRelayClient *m_transport = nullptr;
  QSet<QString> m_desiredTopics;
  QSet<QString> m_activeTopics;
  QHash<quint64, PendingCommand> m_pendingCommands;
  QString m_lastEventId;
  bool m_hasConnected = false;
  bool m_reconnectPending = false;
};

} // namespace HubSight::Admin

Q_DECLARE_METATYPE(HubSight::Admin::RelayEvent)
