#pragma once

#include "../admin_export.h"
#include "live_types.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace HubSight::Admin {

class AdminTransport;
class AdminClient;
struct TransportRequest;
struct TransportResponse;

// Typed REST client for the live capability and WebRTC session endpoints. It
// performs signaling/session control only; media and the standard JSON relay
// remain separate layers.
class HUBSIGHT_ADMIN_EXPORT LiveClient final : public QObject {
  Q_OBJECT

public:
  void fetchCapabilities();
  void listCameras(const QString &cursor = {}, int limit = 50);
  void negotiate(const QString &cameraId, const QString &profile = {},
                 const QJsonObject &options = {});
  void heartbeat(const QString &sessionId, const QJsonObject &metrics = {});
  void release(const QString &sessionId);
  void changeProfile(const QString &sessionId, const QString &profile,
                     const QJsonObject &options = {});
  void fetchSessionStats(const QString &sessionId = {});
  void reportQoe(const QString &sessionId, const QJsonObject &qoe);
  void fetchCameraStatus(const QString &cameraId);

signals:
  void capabilitiesReceived(HubSight::Admin::LiveCapabilities capabilities);
  void camerasReceived(HubSight::Admin::LiveCameraPage page);
  void sessionNegotiated(HubSight::Admin::LiveSession session);
  void sessionHeartbeatReceived(QString sessionId, QJsonObject response);
  void sessionReleased(QString sessionId);
  void sessionProfileChanged(HubSight::Admin::LiveSession session);
  void sessionStatsReceived(HubSight::Admin::LiveSessionStats stats);
  void qoeReported(QString sessionId, QJsonObject response);
  void cameraStatusReceived(HubSight::Admin::LiveCameraStatus status);
  void operationCompleted(QString operation, QJsonObject response);
  void errorOccurred(HubSight::Admin::AdminError error);

private:
  enum class PendingKind {
    Capabilities,
    Cameras,
    Negotiate,
    Heartbeat,
    Release,
    ChangeProfile,
    SessionStats,
    Qoe,
    CameraStatus,
  };

  struct PendingRequest {
    PendingKind kind;
    QString sessionId;
    QString cameraId;
  };

  friend class AdminClient;
  LiveClient(AdminTransport *transport, QObject *parent = nullptr);

  void sendError(const AdminError &error);
  void sendRequest(PendingKind kind, const TransportRequest &request,
                   const QString &sessionId = {}, const QString &cameraId = {});
  void handleResponse(quint64 requestId, const TransportResponse &response);

  AdminTransport *m_transport = nullptr;
  QHash<quint64, PendingRequest> m_pending;
};

} // namespace HubSight::Admin
