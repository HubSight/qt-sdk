#include "../include/hubsight/admin/admin_client.h"

#include "admin_transport.h"

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

  m_storage =
      storage ? std::move(storage) : std::make_shared<InMemorySecureStorage>();
  m_transport = std::make_unique<AdminTransport>(this);
  m_auth = std::unique_ptr<AuthManager>(
      new AuthManager(m_transport.get(), m_storage, this));
  m_system =
      std::unique_ptr<SystemClient>(new SystemClient(m_transport.get(), this));
  m_cameras =
      std::unique_ptr<CameraClient>(new CameraClient(m_transport.get(), this));
  m_realtime = std::unique_ptr<SocketIoClient>(new SocketIoClient(this));
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
              m_webrtc->closeAll();
            }
          });
  connect(m_auth.get(), &AuthManager::errorOccurred, this,
          [this](const AdminError &error) { handleError(error); });
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
  m_transport->cancelAll();
  m_storage =
      storage ? std::move(storage) : std::make_shared<InMemorySecureStorage>();
  m_auth->setSecureStorage(m_storage);
}

SecureStoragePtr AdminClient::secureStorage() const { return m_storage; }

AuthManager *AdminClient::auth() const { return m_auth.get(); }

SystemClient *AdminClient::system() const { return m_system.get(); }

CameraClient *AdminClient::cameras() const { return m_cameras.get(); }

SocketIoClient *AdminClient::realtime() const { return m_realtime.get(); }

WebRtcClient *AdminClient::webrtc() const { return m_webrtc.get(); }

AdminEndpointClient *AdminClient::api() const { return m_api.get(); }

AdminState AdminClient::state() const { return m_state; }

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
  } else {
    m_realtime->setHeader(QByteArrayLiteral("Authorization"),
                          QByteArrayLiteral("Bearer ") +
                              accessToken.trimmed().toUtf8());
  }

  if (m_realtime->state() != SocketIoClient::State::Disconnected) {
    m_realtime->reconnect();
  }
}

} // namespace HubSight::Admin
