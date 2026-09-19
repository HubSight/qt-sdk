#pragma once

#include "../admin_export.h"
#include "../admin_types.h"
#include "notification_types.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

namespace HubSight::Admin {

class AdminTransport;
class AdminClient;
struct TransportRequest;
struct TransportResponse;

// Typed REST client for the normative Admin notifications and push contract.
// It does not substitute the sibling backend's legacy notification routes.
class HUBSIGHT_ADMIN_EXPORT NotificationClient final : public QObject {
  Q_OBJECT

public:
  void list(const QString &cursor = {}, int limit = 50,
            const QJsonObject &filters = {});
  void fetch(const QString &notificationId);
  void patch(const QString &notificationId, const QJsonObject &changes);
  void markAllRead(const QJsonObject &options = {});
  void remove(const QString &notificationId,
              const QString &confirmation = QStringLiteral("yes"));
  void removeBatch(const QStringList &notificationIds,
                   const QString &confirmation = QStringLiteral("yes"));
  void clear(const QString &confirmation = QStringLiteral("yes"));
  void sendTest(const QJsonObject &payload = {});
  void fetchPushConfig();
  void upsertPushSubscription(const QJsonObject &subscription);
  void removePushSubscription();

signals:
  void pageReceived(HubSight::Admin::NotificationPage page);
  void notificationReceived(HubSight::Admin::Notification notification);
  void notificationPatched(QString notificationId,
                           HubSight::Admin::NotificationActionResult result);
  void allNotificationsRead(HubSight::Admin::NotificationActionResult result);
  void notificationDeleted(QString notificationId,
                           HubSight::Admin::NotificationActionResult result);
  void
  notificationsBatchDeleted(HubSight::Admin::NotificationActionResult result);
  void notificationsCleared(HubSight::Admin::NotificationActionResult result);
  void testSent(HubSight::Admin::NotificationActionResult result);
  void pushConfigReceived(HubSight::Admin::NotificationPushConfig config);
  void
  pushSubscriptionUpdated(HubSight::Admin::NotificationActionResult result);
  void pushSubscriptionRemoved();
  void operationCompleted(QString operation, QJsonObject response);
  void errorOccurred(HubSight::Admin::AdminError error);

private:
  enum class PendingKind {
    List,
    Fetch,
    Patch,
    MarkAllRead,
    Delete,
    BatchDelete,
    Clear,
    Test,
    PushConfig,
    PushSubscriptionPut,
    PushSubscriptionDelete,
  };

  struct PendingRequest {
    PendingKind kind;
    QString notificationId;
  };

  friend class AdminClient;
  NotificationClient(AdminTransport *transport, QObject *parent = nullptr);

  void sendError(const AdminError &error);
  void sendRequest(PendingKind kind, const TransportRequest &request,
                   const QString &notificationId = {});
  void handleResponse(quint64 requestId, const TransportResponse &response);

  AdminTransport *m_transport = nullptr;
  QHash<quint64, PendingRequest> m_pending;
};

} // namespace HubSight::Admin
