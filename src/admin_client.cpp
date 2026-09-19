#include "../include/hubsight/admin/admin_client.h"

#include "admin_transport.h"

#include <QFile>
#include <QMetaType>

namespace HubSight::Admin {
namespace {

QUrl normalizeGatewayUrl(const QUrl &gatewayUrl, AdminError *error) {
  const bool loopbackHost =
      gatewayUrl.host().compare(QStringLiteral("localhost"),
                                Qt::CaseInsensitive) == 0 ||
      gatewayUrl.host() == QStringLiteral("127.0.0.1") ||
      gatewayUrl.host() == QStringLiteral("::1");
  if (!gatewayUrl.isValid() || gatewayUrl.host().isEmpty() ||
      (gatewayUrl.scheme() != QStringLiteral("https") &&
       !(gatewayUrl.scheme() == QStringLiteral("http") && loopbackHost)) ||
      !gatewayUrl.userInfo().isEmpty()) {
    if (error) {
      error->category = ErrorCategory::Configuration;
      error->serverCode = QStringLiteral("INVALID_GATEWAY_URL");
      error->developerMessage =
          QStringLiteral("Admin gateway URL must use HTTPS; HTTP is allowed "
                         "only for loopback tests.");
      error->operation = QStringLiteral("client.configure");
    }
    return {};
  }

  QUrl normalized = gatewayUrl;
  if (!normalized.query().isEmpty() || !normalized.fragment().isEmpty()) {
    if (error) {
      error->category = ErrorCategory::Configuration;
      error->serverCode = QStringLiteral("INVALID_GATEWAY_URL");
      error->developerMessage = QStringLiteral(
          "Gateway URL must not contain query or fragment credentials.");
      error->operation = QStringLiteral("client.configure");
    }
    return {};
  }

  QString path = normalized.path();
  while (path.endsWith(QLatin1Char('/'))) {
    path.chop(1);
  }
  if (path.endsWith(QStringLiteral("/api/app/v1"))) {
    if (error) {
      error->category = ErrorCategory::Configuration;
      error->serverCode = QStringLiteral("LEGACY_API_URL_NOT_ALLOWED");
      error->developerMessage =
          QStringLiteral("The Admin SDK cannot be configured with the legacy "
                         "App API namespace.");
      error->operation = QStringLiteral("client.configure");
    }
    return {};
  }
  if (path.endsWith(QStringLiteral("/api/admin/v1"))) {
    path.chop(QStringLiteral("/api/admin/v1").size());
    while (path.endsWith(QLatin1Char('/'))) {
      path.chop(1);
    }
  }
  normalized.setPath(path);
  return normalized;
}

QUrl websocketOrigin(const QUrl &gateway, const QUrl &configuredUrl) {
  QUrl origin = configuredUrl;
  origin.setPath(QString());
  origin.setQuery(QString());
  origin.setFragment(QString());
  if (origin.scheme() == QStringLiteral("https")) {
    origin.setScheme(QStringLiteral("wss"));
  } else if (origin.scheme() == QStringLiteral("http")) {
    origin.setScheme(QStringLiteral("ws"));
  }
  if (origin.host().isEmpty()) {
    origin = gateway;
    origin.setScheme(gateway.scheme() == QStringLiteral("https")
                         ? QStringLiteral("wss")
                         : QStringLiteral("ws"));
    origin.setPath(QString());
  }
  return origin;
}

} // namespace

AdminClient::AdminClient(QObject *parent) : QObject(parent) { initialize({}); }

AdminClient::AdminClient(SecureStoragePtr storage, QObject *parent)
    : QObject(parent) {
  initialize(std::move(storage));
}

AdminClient::~AdminClient() = default;

void AdminClient::initialize(SecureStoragePtr storage) {
  qRegisterMetaType<AdminState>();
  qRegisterMetaType<HttpProtocol>();
  qRegisterMetaType<AdminError>();
  qRegisterMetaType<MaintenanceInfo>();
  qRegisterMetaType<TokenSet>();
  qRegisterMetaType<AdminUser>();
  qRegisterMetaType<SystemStatus>();
  qRegisterMetaType<Capabilities>();
  qRegisterMetaType<CameraSummary>();
  qRegisterMetaType<CameraPage>();
  qRegisterMetaType<RecordingSegment>();
  qRegisterMetaType<ArchiveTimelinePage>();
  qRegisterMetaType<ArchiveAvailableDays>();
  qRegisterMetaType<ArchiveUrlResult>();
  qRegisterMetaType<Notification>();
  qRegisterMetaType<NotificationPage>();
  qRegisterMetaType<NotificationActionResult>();
  qRegisterMetaType<NotificationPushConfig>();
  qRegisterMetaType<RealtimeEvent>();
  qRegisterMetaType<RelayEvent>();
  qRegisterMetaType<RelayError>();

  m_storage =
      storage ? std::move(storage) : std::make_shared<InMemorySecureStorage>();
  m_transport = std::make_unique<AdminTransport>(this);
  m_auth = std::unique_ptr<AuthManager>(
      new AuthManager(m_transport.get(), m_storage, this));
  m_system =
      std::unique_ptr<SystemClient>(new SystemClient(m_transport.get(), this));
  m_cameras =
      std::unique_ptr<CameraClient>(new CameraClient(m_transport.get(), this));
  m_live = std::unique_ptr<LiveClient>(new LiveClient(m_transport.get(), this));
  m_archive = std::unique_ptr<ArchiveClient>(
      new ArchiveClient(m_transport.get(), this));
  m_notifications = std::unique_ptr<NotificationClient>(
      new NotificationClient(m_transport.get(), this));
  m_realtime = std::unique_ptr<SocketIoClient>(new SocketIoClient(this));
  m_socketIoRealtime = std::unique_ptr<SocketIoRealtimeClient>(
      new SocketIoRealtimeClient(m_realtime.get(), this));
  m_relay = std::unique_ptr<StandardRelayClient>(new StandardRelayClient(this));
  m_relayRealtime = std::unique_ptr<RelayRealtimeClient>(
      new RelayRealtimeClient(m_relay.get(), this));
  m_webrtc = std::unique_ptr<WebRtcClient>(new WebRtcClient(this));
  m_api = std::unique_ptr<AdminEndpointClient>(
      new AdminEndpointClient(m_transport.get(), this));

  connect(m_auth.get(), &AuthManager::stateChanged, this,
          [this](AdminState state) {
            setState(state);
            if (state == AdminState::Unauthenticated ||
                state == AdminState::Revoked) {
              m_realtime->removeHeader(QByteArrayLiteral("Authorization"));
              m_realtime->disconnectFromServer(
                  QStringLiteral("Admin authentication session ended"));
              m_relay->removeHeader(QByteArrayLiteral("Authorization"));
              m_relayRealtime->clearTopicSubscriptions();
              m_relayRealtime->clearLastEventId();
              m_relay->disconnectFromServer(
                  QStringLiteral("Admin authentication session ended"));
              m_webrtc->closeAll();
            }
          });
  connect(m_auth.get(), &AuthManager::errorOccurred, this,
          [this](const AdminError &error) { handleError(error); });
  connect(m_socketIoRealtime.get(), &SocketIoRealtimeClient::sessionRevoked,
          this, [this](const RealtimeEvent &) { m_auth->invalidateSession(); });
  connect(m_socketIoRealtime.get(), &SocketIoRealtimeClient::forceLogout, this,
          [this](const RealtimeEvent &) { m_auth->invalidateSession(); });
  connect(m_relayRealtime.get(), &RelayRealtimeClient::sessionRevoked, this,
          [this](const RelayEvent &) { m_auth->invalidateSession(); });
  connect(m_relayRealtime.get(), &RelayRealtimeClient::forceLogout, this,
          [this](const RelayEvent &) { m_auth->invalidateSession(); });
  connect(m_relayRealtime.get(), &RelayRealtimeClient::adminApiDisabled, this,
          [this](const RelayEvent &) { m_auth->invalidateSession(); });
  connect(m_auth.get(), &AuthManager::loginSucceeded, this,
          [this](const TokenSet &tokens, const AdminUser &) {
            setRealtimeAccessToken(tokens.accessToken);
          });
  connect(m_auth.get(), &AuthManager::tokenRefreshed, this,
          [this](const TokenSet &tokens) {
            setRealtimeAccessToken(tokens.accessToken);
          });
  connect(m_system.get(), &SystemClient::errorOccurred, this,
          [this](const AdminError &error) { handleError(error); });
  connect(m_cameras.get(), &CameraClient::errorOccurred, this,
          [this](const AdminError &error) { handleError(error); });
  connect(m_live.get(), &LiveClient::errorOccurred, this,
          [this](const AdminError &error) { handleError(error); });
  connect(m_archive.get(), &ArchiveClient::errorOccurred, this,
          [this](const AdminError &error) { handleError(error); });
  connect(m_notifications.get(), &NotificationClient::errorOccurred, this,
          [this](const AdminError &error) { handleError(error); });
  connect(m_api.get(), &AdminEndpointClient::errorOccurred, this,
          [this](const AdminError &error) { handleError(error); });
  connect(m_transport.get(), &AdminTransport::finished, this,
          [this](quint64, const TransportResponse &response) {
            emit requestCompleted(response.operation, response.protocol);
          });
  connect(m_transport.get(), &AdminTransport::maintenanceDetected, this,
          [this](const MaintenanceInfo &info) {
            setState(AdminState::Maintenance);
            emit maintenanceChanged(info);
          });
}

bool AdminClient::setGatewayUrl(const QUrl &gatewayUrl) {
  AdminError error;
  const QUrl normalized = normalizeGatewayUrl(gatewayUrl, &error);
  if (normalized.isEmpty() || !m_transport->setGatewayUrl(normalized)) {
    if (error.serverCode.isEmpty()) {
      error.category = ErrorCategory::Configuration;
      error.serverCode = QStringLiteral("INVALID_GATEWAY_URL");
      error.developerMessage =
          QStringLiteral("Unable to configure the Admin gateway URL.");
      error.operation = QStringLiteral("client.configure");
    }
    handleError(error);
    return false;
  }

  m_auth->invalidateSession();
  m_webrtc->closeAll();
  m_realtime->disconnectFromServer(QStringLiteral("Admin gateway changed"));
  m_realtime->removeHeader(QByteArrayLiteral("Authorization"));
  m_realtime->setBaseUrl(normalized);
  m_relay->disconnectFromServer(QStringLiteral("Admin gateway changed"));
  m_relayRealtime->clearTopicSubscriptions();
  m_relayRealtime->clearLastEventId();
  m_relay->removeHeader(QByteArrayLiteral("Authorization"));
  m_relay->setBaseUrl(websocketOrigin(normalized, normalized));
  m_hasImportedConfig = false;
  m_importedConfig = {};
  m_importedIntegrity = HscfgIntegrityState::NotChecked;
  m_transport->cancelAll();
  setState(m_transport->isConfigured() ? AdminState::Unauthenticated
                                       : AdminState::Unconfigured);
  return true;
}

QUrl AdminClient::gatewayUrl() const { return m_transport->gatewayUrl(); }

void AdminClient::setApiKey(const QString &apiKey) {
  m_auth->invalidateSession();
  m_webrtc->closeAll();
  m_realtime->disconnectFromServer(QStringLiteral("Admin API key changed"));
  m_realtime->removeHeader(QByteArrayLiteral("Authorization"));
  m_realtime->setHeader(QByteArrayLiteral("X-API-Key"),
                        apiKey.trimmed().toUtf8());
  m_relay->disconnectFromServer(QStringLiteral("Admin API key changed"));
  m_relayRealtime->clearTopicSubscriptions();
  m_relayRealtime->clearLastEventId();
  m_relay->removeHeader(QByteArrayLiteral("Authorization"));
  m_relay->setHeader(QByteArrayLiteral("X-API-Key"), apiKey.trimmed().toUtf8());
  m_hasImportedConfig = false;
  m_importedConfig = {};
  m_importedIntegrity = HscfgIntegrityState::NotChecked;
  m_transport->cancelAll();
  m_transport->setApiKey(apiKey);
  setState(m_transport->isConfigured() ? AdminState::Unauthenticated
                                       : AdminState::Unconfigured);
}

bool AdminClient::isConfigured() const { return m_transport->isConfigured(); }

void AdminClient::setSecureStorage(SecureStoragePtr storage) {
  m_auth->invalidateSession();
  m_webrtc->closeAll();
  m_realtime->disconnectFromServer(
      QStringLiteral("Admin secure storage changed"));
  m_realtime->removeHeader(QByteArrayLiteral("Authorization"));
  m_relay->disconnectFromServer(QStringLiteral("Admin secure storage changed"));
  m_relayRealtime->clearTopicSubscriptions();
  m_relayRealtime->clearLastEventId();
  m_relay->removeHeader(QByteArrayLiteral("Authorization"));
  m_transport->cancelAll();
  m_storage =
      storage ? std::move(storage) : std::make_shared<InMemorySecureStorage>();
  m_auth->setSecureStorage(m_storage);
}

SecureStoragePtr AdminClient::secureStorage() const { return m_storage; }

AuthManager *AdminClient::auth() const { return m_auth.get(); }

SystemClient *AdminClient::system() const { return m_system.get(); }

CameraClient *AdminClient::cameras() const { return m_cameras.get(); }

LiveClient *AdminClient::live() const { return m_live.get(); }

ArchiveClient *AdminClient::archive() const { return m_archive.get(); }

NotificationClient *AdminClient::notifications() const {
  return m_notifications.get();
}

SocketIoClient *AdminClient::realtime() const { return m_realtime.get(); }

SocketIoRealtimeClient *AdminClient::socketIoRealtime() const {
  return m_socketIoRealtime.get();
}

StandardRelayClient *AdminClient::relay() const { return m_relay.get(); }

RelayRealtimeClient *AdminClient::relayRealtime() const {
  return m_relayRealtime.get();
}

void AdminClient::setHscfgTrustedEd25519PublicKey(const QByteArray &publicKey) {
  m_hscfgImporter.setTrustedEd25519PublicKey(publicKey);
}

void AdminClient::setHscfgRequireFullIntegrity(bool required) {
  m_requireFullHscfgIntegrity = required;
}

bool AdminClient::hscfgRequireFullIntegrity() const {
  return m_requireFullHscfgIntegrity;
}

bool AdminClient::importHscfg(const QByteArray &data, const QString &pin) {
  const HscfgImportResult result = m_hscfgImporter.importAdmin(data, pin);
  if (!result.success()) {
    AdminError error;
    error.category = ErrorCategory::Configuration;
    error.serverCode =
        QStringLiteral("HSCFG_") + toString(result.error).toUpper();
    error.developerMessage = result.message;
    error.operation = QStringLiteral("client.importHscfg");
    handleError(error);
    return false;
  }
  if (m_requireFullHscfgIntegrity &&
      result.integrity != HscfgIntegrityState::FullyVerified) {
    AdminError error;
    error.category = ErrorCategory::Configuration;
    error.serverCode = QStringLiteral("HSCFG_SIGNATURE_UNAVAILABLE");
    error.developerMessage = QStringLiteral(
        "The Admin .hscfg content hash is valid, but full Ed25519 integrity "
        "verification is required by this client.");
    error.operation = QStringLiteral("client.importHscfg");
    handleError(error);
    return false;
  }
  if (!applyImportedConfig(result.config)) {
    return false;
  }
  m_importedIntegrity = result.integrity;
  return true;
}

bool AdminClient::importHscfgFile(const QString &path, const QString &pin) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    AdminError error;
    error.category = ErrorCategory::Configuration;
    error.serverCode = QStringLiteral("HSCFG_FILE_READ_FAILED");
    error.developerMessage =
        QStringLiteral("Unable to read the .hscfg file. The PIN and API key "
                       "were not exposed.");
    error.operation = QStringLiteral("client.importHscfg");
    handleError(error);
    return false;
  }
  if (file.size() > 64 * 1024 * 1024) {
    AdminError error;
    error.category = ErrorCategory::Configuration;
    error.serverCode = QStringLiteral("HSCFG_CONTAINER_TOO_LARGE");
    error.developerMessage = QStringLiteral("The .hscfg file is too large.");
    error.operation = QStringLiteral("client.importHscfg");
    handleError(error);
    return false;
  }
  return importHscfg(file.readAll(), pin);
}

void AdminClient::clearImportedConfig() {
  m_auth->invalidateSession();
  m_webrtc->closeAll();
  m_realtime->clearConfiguration();
  m_relayRealtime->clearTopicSubscriptions();
  m_relayRealtime->clearLastEventId();
  m_relay->clearConfiguration();
  m_transport->clearConnection();
  m_importedConfig = {};
  m_hasImportedConfig = false;
  m_importedIntegrity = HscfgIntegrityState::NotChecked;
  setState(AdminState::Unconfigured);
}

bool AdminClient::hasImportedConfig() const { return m_hasImportedConfig; }

HscfgConfig AdminClient::importedConfig() const { return m_importedConfig; }

HscfgIntegrityState AdminClient::importedConfigIntegrity() const {
  return m_importedIntegrity;
}

WebRtcClient *AdminClient::webrtc() const { return m_webrtc.get(); }

AdminEndpointClient *AdminClient::api() const { return m_api.get(); }

AdminState AdminClient::state() const { return m_state; }

bool AdminClient::applyImportedConfig(const HscfgConfig &config) {
  QString reason;
  if (!config.isValid(&reason)) {
    AdminError error;
    error.category = ErrorCategory::Configuration;
    error.serverCode = QStringLiteral("HSCFG_INVALID_CONFIG");
    error.developerMessage = reason;
    error.operation = QStringLiteral("client.importHscfg");
    handleError(error);
    return false;
  }

  AdminError gatewayError;
  const QUrl gateway =
      normalizeGatewayUrl(config.urls.gatewayUrl, &gatewayError);
  if (gateway.isEmpty()) {
    handleError(gatewayError);
    return false;
  }

  const QUrl relayOrigin =
      websocketOrigin(gateway, config.urls.relayWebSocketUrl);
  if (!m_realtime->setBaseUrl(relayOrigin) ||
      !m_realtime->setPath(config.metadata.realtimeNamespace +
                           QStringLiteral("/socket.io")) ||
      !m_relay->setBaseUrl(relayOrigin) ||
      !m_relay->setPath(config.metadata.realtimeNamespace) ||
      !m_webrtc->setEndpointUrls(config.urls.webRtcBaseUrl,
                                 config.urls.webRtcSignalingUrl,
                                 config.urls.webRtcMediaPort) ||
      !m_transport->setConnection(gateway, config.identity.apiKey)) {
    AdminError error;
    error.category = ErrorCategory::Configuration;
    error.serverCode = QStringLiteral("HSCFG_APPLY_FAILED");
    error.developerMessage = QStringLiteral(
        "Unable to apply the Admin .hscfg connection atomically.");
    error.operation = QStringLiteral("client.importHscfg");
    handleError(error);
    return false;
  }

  m_auth->invalidateSession();
  m_webrtc->closeAll();
  m_realtime->disconnectFromServer(QStringLiteral("Admin .hscfg imported"));
  m_realtime->removeHeader(QByteArrayLiteral("Authorization"));
  m_realtime->setHeader(QByteArrayLiteral("X-API-Key"),
                        config.identity.apiKey.toUtf8());
  m_relay->disconnectFromServer(QStringLiteral("Admin .hscfg imported"));
  m_relayRealtime->clearTopicSubscriptions();
  m_relayRealtime->clearLastEventId();
  m_relay->removeHeader(QByteArrayLiteral("Authorization"));
  m_relay->setHeader(QByteArrayLiteral("X-API-Key"),
                     config.identity.apiKey.toUtf8());
  m_transport->cancelAll();

  m_importedConfig = config;
  m_hasImportedConfig = true;
  setState(AdminState::Unauthenticated);
  return true;
}

void AdminClient::setState(AdminState state) {
  if (m_state == state) {
    return;
  }
  m_state = state;
  emit stateChanged(m_state);
}

void AdminClient::handleError(const AdminError &error) {
  if (error.isMaintenance()) {
    setState(AdminState::Maintenance);
  } else if (error.isNetworkError()) {
    setState(AdminState::Offline);
  } else if (error.isAuthenticationError() &&
             (error.operation == QStringLiteral("auth.login") ||
              error.operation == QStringLiteral("auth.2fa.verify"))) {
    setState(AdminState::Unauthenticated);
  }
  emit errorOccurred(error);
}

void AdminClient::setRealtimeAccessToken(const QString &accessToken) {
  if (accessToken.trimmed().isEmpty()) {
    m_realtime->removeHeader(QByteArrayLiteral("Authorization"));
    m_relay->removeHeader(QByteArrayLiteral("Authorization"));
  } else {
    const QByteArray authorization =
        QByteArrayLiteral("Bearer ") + accessToken.trimmed().toUtf8();
    m_realtime->setHeader(QByteArrayLiteral("Authorization"), authorization);
    m_relay->setHeader(QByteArrayLiteral("Authorization"), authorization);
  }

  if (m_realtime->state() != SocketIoClient::State::Disconnected) {
    m_realtime->reconnect();
  }
  if (m_relay->state() != StandardRelayClient::State::Disconnected) {
    m_relay->reconnect();
  }
}

} // namespace HubSight::Admin
