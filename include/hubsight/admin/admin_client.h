#pragma once

#include "admin_endpoint_client.h"
#include "admin_export.h"
#include "admin_types.h"
#include "auth_manager.h"
#include "realtime/socket_io_client.h"
#include "resources/camera_client.h"
#include "resources/system_client.h"
#include "secure_storage.h"
#include "webrtc/webrtc_client.h"

#include <QObject>
#include <QUrl>

#include <memory>

namespace HubSight::Admin {

class AdminTransport;

class HUBSIGHT_ADMIN_EXPORT AdminClient final : public QObject {
  Q_OBJECT

public:
  explicit AdminClient(QObject *parent = nullptr);
  explicit AdminClient(SecureStoragePtr storage, QObject *parent = nullptr);
  ~AdminClient() override;

  // gatewayUrl must be an HTTPS gateway origin. HTTP is accepted only for
  // localhost/loopback tests. The SDK appends the dedicated /api/admin/v1
  // namespace and never uses /api/app/v1.
  bool setGatewayUrl(const QUrl &gatewayUrl);
  QUrl gatewayUrl() const;

  // The key is sent only as X-API-Key. It is never put in a URL or persisted
  // by the SDK.
  void setApiKey(const QString &apiKey);
  bool isConfigured() const;

  // Replace the token store before authenticating. Production applications
  // should provide an OS-backed implementation.
  void setSecureStorage(SecureStoragePtr storage);
  SecureStoragePtr secureStorage() const;

  AuthManager *auth() const;
  SystemClient *system() const;
  CameraClient *cameras() const;

  // Base Socket.IO client for future realtime/domain clients. It is not
  // connected automatically; configure the path/namespace and call connect.
  SocketIoClient *realtime() const;

  // Backend-neutral WebRTC session registry. Attach a native/libdatachannel
  // adapter to each peer connection before creating an offer or answer.
  WebRtcClient *webrtc() const;

  // Complete endpoint registry/facade. Calls for deferred endpoints emit
  // SDK_ENDPOINT_NOT_IMPLEMENTED until their phase lands.
  AdminEndpointClient *api() const;

  AdminState state() const;

signals:
  void stateChanged(HubSight::Admin::AdminState state);
  void maintenanceChanged(HubSight::Admin::MaintenanceInfo info);
  void errorOccurred(HubSight::Admin::AdminError error);
  void requestCompleted(QString operation,
                        HubSight::Admin::HttpProtocol protocol);

private:
  void initialize(SecureStoragePtr storage);
  void setState(AdminState state);
  void handleError(const AdminError &error);
  void setRealtimeAccessToken(const QString &accessToken);

  std::unique_ptr<AdminTransport> m_transport;
  SecureStoragePtr m_storage;
  std::unique_ptr<AuthManager> m_auth;
  std::unique_ptr<SystemClient> m_system;
  std::unique_ptr<CameraClient> m_cameras;
  std::unique_ptr<SocketIoClient> m_realtime;
  std::unique_ptr<WebRtcClient> m_webrtc;
  std::unique_ptr<AdminEndpointClient> m_api;
  AdminState m_state = AdminState::Unconfigured;
};

} // namespace HubSight::Admin
