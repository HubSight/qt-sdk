#pragma once

#include "admin_endpoint_client.h"
#include "admin_export.h"
#include "admin_types.h"
#include "auth_manager.h"
#include "config/hscfg_importer.h"
#include "config/hscfg_types.h"
#include "realtime/relay_client.h"
#include "realtime/relay_realtime_client.h"
#include "realtime/socket_io_client.h"
#include "realtime/socket_io_realtime_client.h"
#include "resources/account_client.h"
#include "resources/archive_client.h"
#include "resources/camera_client.h"
#include "resources/camera_management_client.h"
#include "resources/identity_client.h"
#include "resources/integration_client.h"
#include "resources/live_client.h"
#include "resources/member_client.h"
#include "resources/notification_client.h"
#include "resources/system_client.h"
#include "resources/system_operations_client.h"
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

  // Advanced/core API. Normal applications should use
  // AdminApplicationClient, which owns the JWT lifecycle for them.
  AuthManager *auth() const;
  AccountClient *account() const;
  SystemClient *system() const;
  SystemOperationsClient *systemOperations() const;
  CameraClient *cameras() const;
  CameraManagementClient *cameraManagement() const;
  IdentityClient *identity() const;
  IntegrationClient *integrations() const;
  MemberClient *members() const;
  LiveClient *live() const;
  ArchiveClient *archive() const;
  NotificationClient *notifications() const;

  // Advanced compatibility API. Normal applications should use
  // AdminApplicationClient::realtime(), which never requires Socket.IO
  // selection or packet handling.
  // The normative Admin JSON relay is exposed separately below.
  SocketIoClient *realtime() const;

  // Socket.IO domain events and additional-room lifecycle. This is an optional
  // compatibility layer over realtime(); it does not replace relay().
  SocketIoRealtimeClient *socketIoRealtime() const;

  // Standard JSON WebSocket relay transport. It is intentionally separate
  // from Socket.IO and is configured by the imported Admin .hscfg profile.
  StandardRelayClient *relay() const;

  // Typed domain layer for the normative /relay/admin/v1 JSON contract. It
  // exposes only the allowlisted Admin topics and replay lifecycle.
  RelayRealtimeClient *relayRealtime() const;

  void setHscfgTrustedEd25519PublicKey(const QByteArray &publicKey);
  void setHscfgRequireFullIntegrity(bool required);
  bool hscfgRequireFullIntegrity() const;
  bool importHscfg(const QByteArray &data, const QString &pin);
  bool importHscfgFile(const QString &path, const QString &pin);
  void clearImportedConfig();
  bool hasImportedConfig() const;
  HscfgConfig importedConfig() const;
  HscfgIntegrityState importedConfigIntegrity() const;

  // Advanced media integration API. Normal applications do not need to attach
  // a native/libdatachannel adapter directly; the app-facing facade keeps this
  // registry out of the normal application surface.
  WebRtcClient *webrtc() const;

  // Complete endpoint registry/facade. Typed resource clients are preferred;
  // this generic client can invoke every catalog endpoint with path/query/body
  // JSON request fields.
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
  bool applyImportedConfig(const HscfgConfig &config);

  std::unique_ptr<AdminTransport> m_transport;
  SecureStoragePtr m_storage;
  std::unique_ptr<AuthManager> m_auth;
  std::unique_ptr<AccountClient> m_account;
  std::unique_ptr<SystemClient> m_system;
  std::unique_ptr<SystemOperationsClient> m_systemOperations;
  std::unique_ptr<CameraClient> m_cameras;
  std::unique_ptr<CameraManagementClient> m_cameraManagement;
  std::unique_ptr<IdentityClient> m_identity;
  std::unique_ptr<IntegrationClient> m_integrations;
  std::unique_ptr<MemberClient> m_members;
  std::unique_ptr<LiveClient> m_live;
  std::unique_ptr<ArchiveClient> m_archive;
  std::unique_ptr<NotificationClient> m_notifications;
  std::unique_ptr<SocketIoClient> m_realtime;
  std::unique_ptr<SocketIoRealtimeClient> m_socketIoRealtime;
  std::unique_ptr<StandardRelayClient> m_relay;
  std::unique_ptr<RelayRealtimeClient> m_relayRealtime;
  std::unique_ptr<WebRtcClient> m_webrtc;
  std::unique_ptr<AdminEndpointClient> m_api;
  HscfgImporter m_hscfgImporter;
  HscfgConfig m_importedConfig;
  bool m_hasImportedConfig = false;
  bool m_requireFullHscfgIntegrity = false;
  HscfgIntegrityState m_importedIntegrity = HscfgIntegrityState::NotChecked;
  AdminState m_state = AdminState::Unconfigured;
};

} // namespace HubSight::Admin
