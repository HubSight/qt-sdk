#pragma once

#include "../admin_export.h"
#include "../resources/notification_types.h"
#include "socket_io_client.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>

namespace HubSight::Admin {

class AdminClient;

struct HUBSIGHT_ADMIN_EXPORT RealtimeEvent {
  QString name;
  QJsonObject data;
  QDateTime timestamp;
  QString sender;
  QString sourceSocketId;
  QJsonObject raw;

  bool isValid() const;
  static RealtimeEvent fromSocketIo(const QString &event,
                                    const QJsonArray &arguments);
};

// Domain layer on top of the Socket.IO transport foundation. This client is
// intended for deployments that explicitly expose the legacy/compatible
// Socket.IO relay. The normative Admin /relay/admin/v1 contract remains the
// StandardRelayClient and is not routed through this class.
class HUBSIGHT_ADMIN_EXPORT SocketIoRealtimeClient final : public QObject {
  Q_OBJECT

public:
  explicit SocketIoRealtimeClient(SocketIoClient *transport,
                                  QObject *parent = nullptr);

  SocketIoClient *transport() const;
  QStringList rooms() const;

  // The backend automatically assigns authenticated user/role/session rooms.
  // These methods manage additional rooms and rejoin them after reconnect.
  quint64 subscribeRoom(const QString &room);
  quint64 unsubscribeRoom(const QString &room);
  void clearRoomSubscriptions();

signals:
  void roomJoined(QString room);
  void roomLeft(QString room);
  void roomOperationFailed(QString room, QString operation, QString message);
  void eventReceived(HubSight::Admin::RealtimeEvent event);
  void cameraEvent(HubSight::Admin::RealtimeEvent event);
  void notificationReceived(HubSight::Admin::Notification notification);
  void poolStatusUpdated(HubSight::Admin::RealtimeEvent event);
  void nvrStatusUpdated(HubSight::Admin::RealtimeEvent event);
  void memberEvent(HubSight::Admin::RealtimeEvent event);
  void visionEvent(HubSight::Admin::RealtimeEvent event);
  void operationEvent(HubSight::Admin::RealtimeEvent event);
  void sessionRevoked(HubSight::Admin::RealtimeEvent event);
  void forceLogout(HubSight::Admin::RealtimeEvent event);
  void errorOccurred(HubSight::Admin::SocketIoError error);

private:
  enum class RoomOperation { Join, Leave };

  struct PendingRoomOperation {
    RoomOperation operation;
    QString room;
  };

  bool validateRoom(const QString &room, QString *normalized,
                    QString *message) const;
  quint64 sendRoomOperation(RoomOperation operation, const QString &room);
  void handleConnected();
  void handleDisconnected();
  void handleAcknowledgement(quint64 acknowledgementId,
                             const QJsonArray &arguments);
  void handleEvent(const QString &event, const QJsonArray &arguments);
  void emitInvalidRoomError(const QString &message);
  void emitRoomError(const QString &room, RoomOperation operation,
                     const QString &message);
  void rejoinRooms();

  SocketIoClient *m_transport = nullptr;
  QSet<QString> m_desiredRooms;
  QSet<QString> m_activeRooms;
  QHash<quint64, PendingRoomOperation> m_pendingRoomOperations;
};

} // namespace HubSight::Admin

Q_DECLARE_METATYPE(HubSight::Admin::RealtimeEvent)
