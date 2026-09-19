#include "../include/hubsight/admin/admin_application_client.h"
#include "../include/hubsight/admin/desktop_secure_storage.h"

#include <QJsonDocument>
#include <QLoggingCategory>
#include <QRegularExpression>

#include <climits>
#include <utility>

Q_LOGGING_CATEGORY(lcHubSightAdminSdk, "hubsight.admin.sdk")

namespace HubSight::Admin {
namespace {

DiagnosticSource sourceForAdminError(const AdminError &error) {
  if (error.operation.startsWith(QStringLiteral("auth.")) ||
      error.category == ErrorCategory::Authentication ||
      error.category == ErrorCategory::Authorization) {
    return DiagnosticSource::Authentication;
  }
  if (error.category == ErrorCategory::Configuration) {
    return DiagnosticSource::Configuration;
  }
  return DiagnosticSource::Http;
}

DiagnosticSeverity severityForAdminError(const AdminError &error) {
  return error.isMaintenance() ? DiagnosticSeverity::Warning
                               : DiagnosticSeverity::Error;
}

QString relayErrorCodeName(RelayErrorCode code) {
  switch (code) {
  case RelayErrorCode::InvalidConfiguration:
    return QStringLiteral("INVALID_CONFIGURATION");
  case RelayErrorCode::ConnectionFailed:
    return QStringLiteral("CONNECTION_FAILED");
  case RelayErrorCode::ProtocolError:
    return QStringLiteral("PROTOCOL_ERROR");
  case RelayErrorCode::Canceled:
    return QStringLiteral("CANCELED");
  case RelayErrorCode::Unknown:
    return QStringLiteral("UNKNOWN");
  }
  return QStringLiteral("UNKNOWN");
}

QString socketIoErrorCodeName(SocketIoErrorCode code) {
  switch (code) {
  case SocketIoErrorCode::InvalidConfiguration:
    return QStringLiteral("INVALID_CONFIGURATION");
  case SocketIoErrorCode::ConnectionFailed:
    return QStringLiteral("CONNECTION_FAILED");
  case SocketIoErrorCode::HandshakeFailed:
    return QStringLiteral("HANDSHAKE_FAILED");
  case SocketIoErrorCode::ProtocolError:
    return QStringLiteral("PROTOCOL_ERROR");
  case SocketIoErrorCode::Timeout:
    return QStringLiteral("TIMEOUT");
  case SocketIoErrorCode::Canceled:
    return QStringLiteral("CANCELED");
  case SocketIoErrorCode::Unsupported:
    return QStringLiteral("UNSUPPORTED");
  case SocketIoErrorCode::Unknown:
    return QStringLiteral("UNKNOWN");
  }
  return QStringLiteral("UNKNOWN");
}

QString adminStateName(AdminState state) {
  switch (state) {
  case AdminState::Unconfigured:
    return QStringLiteral("unconfigured");
  case AdminState::Unauthenticated:
    return QStringLiteral("unauthenticated");
  case AdminState::Authenticating:
    return QStringLiteral("authenticating");
  case AdminState::Ready:
    return QStringLiteral("ready");
  case AdminState::Maintenance:
    return QStringLiteral("maintenance");
  case AdminState::Offline:
    return QStringLiteral("offline");
  case AdminState::Revoked:
    return QStringLiteral("revoked");
  }
  return QStringLiteral("unknown");
}

QJsonObject adminErrorDetails(const AdminError &error) {
  QJsonObject details{
      {QStringLiteral("category"), static_cast<int>(error.category)},
      {QStringLiteral("http_status"), error.httpStatus},
  };
  if (error.retryAfterSeconds > 0) {
    details.insert(QStringLiteral("retry_after_seconds"),
                   error.retryAfterSeconds);
  }
  return details;
}

QString sanitizeDiagnosticMessage(QString message) {
  static const QRegularExpression bearerPattern(
      QStringLiteral(R"((?i)\bBearer\s+[^\s,;]+)"));
  static const QRegularExpression credentialPattern(QStringLiteral(
      R"((?i)(access[_-]?token|refresh[_-]?token|api[_-]?key|password)\s*[:=]\s*[^\s,;]+)"));
  message.replace(bearerPattern, QStringLiteral("Bearer <redacted>"));
  message.replace(credentialPattern, QStringLiteral("<redacted>"));
  return message;
}

} // namespace

AdminApplicationClient::AdminApplicationClient(QObject *parent)
    : QObject(parent) {
  initialize({});
}

AdminApplicationClient::AdminApplicationClient(SecureStoragePtr storage,
                                               QObject *parent)
    : QObject(parent) {
  initialize(std::move(storage));
}

AdminApplicationClient::~AdminApplicationClient() = default;

void AdminApplicationClient::initialize(SecureStoragePtr storage) {
  qRegisterMetaType<SdkDiagnostic>();
  qRegisterMetaType<DiagnosticSeverity>();
  qRegisterMetaType<DiagnosticSource>();

  const SecureStoragePtr effectiveStorage =
      storage ? std::move(storage) : std::make_shared<DesktopSecureStorage>();
  m_client =
      std::unique_ptr<AdminClient>(new AdminClient(effectiveStorage, this));
  m_refreshTimer.setSingleShot(true);
  connect(&m_refreshTimer, &QTimer::timeout, this,
          [this]() { refreshSession(); });

  connect(m_client.get(), &AdminClient::stateChanged, this,
          [this](AdminState state) {
            if (state == AdminState::Unauthenticated ||
                state == AdminState::Revoked) {
              m_preAuthToken.clear();
              m_refreshTimer.stop();
            }
            emit stateChanged(state);
            record(DiagnosticSeverity::Info, DiagnosticSource::Lifecycle,
                   QStringLiteral("STATE_CHANGED"), QStringLiteral("client"),
                   QStringLiteral("Admin client state changed to %1.")
                       .arg(adminStateName(state)));
          });
  connect(m_client.get(), &AdminClient::errorOccurred, this,
          [this](const AdminError &error) {
            recordAdminError(error);
            emit errorOccurred(error);
          });
  connect(
      m_client.get(), &AdminClient::maintenanceChanged, this,
      [this](const MaintenanceInfo &info) { emit maintenanceChanged(info); });
  connect(m_client.get(), &AdminClient::requestCompleted, this,
          [this](const QString &operation, HttpProtocol protocol) {
            emit requestCompleted(operation, protocol);
            record(DiagnosticSeverity::Debug, DiagnosticSource::Http,
                   QStringLiteral("REQUEST_COMPLETED"), operation,
                   QStringLiteral("Admin HTTP request completed using %1.")
                       .arg(static_cast<int>(protocol)));
          });

  connect(m_client->auth(), &AuthManager::twoFactorRequired, this,
          [this](const QString &preAuthToken) {
            handleTwoFactorRequired(preAuthToken);
          });
  connect(m_client->auth(), &AuthManager::loginSucceeded, this,
          [this](const TokenSet &tokens, const AdminUser &user) {
            handleAuthenticated(tokens, user);
          });
  connect(m_client->auth(), &AuthManager::tokenRefreshed, this,
          [this](const TokenSet &tokens) {
            scheduleTokenRefresh(tokens);
            record(DiagnosticSeverity::Info, DiagnosticSource::Authentication,
                   QStringLiteral("TOKEN_REFRESHED"),
                   QStringLiteral("auth.refresh"),
                   QStringLiteral("Admin access token refreshed."));
          });
  connect(m_client->auth(), &AuthManager::currentUserChanged, this,
          [this](const AdminUser &user) { emit currentUserChanged(user); });
  connect(m_client->auth(), &AuthManager::loggedOut, this, [this]() {
    m_preAuthToken.clear();
    m_refreshTimer.stop();
    record(DiagnosticSeverity::Info, DiagnosticSource::Authentication,
           QStringLiteral("SIGNED_OUT"), QStringLiteral("auth.logout"),
           QStringLiteral("Admin session ended."));
    emit signedOut();
  });

  connect(m_client->relay(), &StandardRelayClient::connected, this, [this]() {
    record(DiagnosticSeverity::Info, DiagnosticSource::StandardRelay,
           QStringLiteral("CONNECTED"), QStringLiteral("relay.connect"),
           QStringLiteral("Standard Admin relay connected."));
  });
  connect(m_client->relay(), &StandardRelayClient::disconnected, this,
          [this](const QString &reason) {
            record(DiagnosticSeverity::Info, DiagnosticSource::StandardRelay,
                   QStringLiteral("DISCONNECTED"),
                   QStringLiteral("relay.disconnect"), reason);
          });
  connect(m_client->relayRealtime(), &RelayRealtimeClient::errorOccurred, this,
          [this](const RelayError &error) {
            record(
                error.retryable ? DiagnosticSeverity::Warning
                                : DiagnosticSeverity::Error,
                DiagnosticSource::StandardRelay,
                QStringLiteral("RELAY_%1").arg(relayErrorCodeName(error.code)),
                QStringLiteral("relay"), error.message, {},
                QJsonObject{
                    {QStringLiteral("transport_error"), error.transportError}},
                error.retryable);
          });
  connect(m_client->relayRealtime(), &RelayRealtimeClient::topicOperationFailed,
          this,
          [this](const QString &topic, const QString &operation,
                 const QString &message) {
            record(DiagnosticSeverity::Warning, DiagnosticSource::StandardRelay,
                   QStringLiteral("TOPIC_OPERATION_FAILED"),
                   QStringLiteral("relay.%1").arg(operation), message, {},
                   QJsonObject{{QStringLiteral("topic"), topic}}, true);
          });
  connect(m_client->relayRealtime(), &RelayRealtimeClient::replayCompleted,
          this, [this](bool resumed, const QString &reason) {
            if (!resumed) {
              record(DiagnosticSeverity::Warning,
                     DiagnosticSource::StandardRelay,
                     QStringLiteral("REPLAY_UNAVAILABLE"),
                     QStringLiteral("relay.resume"), reason, {}, {}, true);
            }
          });
  connect(m_client->realtime(), &SocketIoClient::errorOccurred, this,
          [this](const SocketIoError &error) {
            record(error.retryable ? DiagnosticSeverity::Warning
                                   : DiagnosticSeverity::Error,
                   DiagnosticSource::SocketIo,
                   QStringLiteral("SOCKET_IO_%1")
                       .arg(socketIoErrorCodeName(error.code)),
                   QStringLiteral("socket_io"), error.message, {},
                   QJsonObject{{QStringLiteral("transport_error"),
                                error.transportError}},
                   error.retryable);
          });
  connect(m_client->webrtc(), &WebRtcClient::errorOccurred, this,
          [this](const WebRtcError &error) {
            record(error.retryable ? DiagnosticSeverity::Warning
                                   : DiagnosticSeverity::Error,
                   DiagnosticSource::WebRtc,
                   QStringLiteral("WEBRTC_%1").arg(toString(error.code)),
                   error.operation, error.message, {}, {}, error.retryable);
          });

  const QString backend = m_client->secureStorage()->backendName();
  record(backend == QStringLiteral("unsupported-desktop-platform")
             ? DiagnosticSeverity::Error
             : DiagnosticSeverity::Info,
         DiagnosticSource::Configuration,
         backend == QStringLiteral("unsupported-desktop-platform")
             ? QStringLiteral("SECURE_STORAGE_UNAVAILABLE")
             : QStringLiteral("SECURE_STORAGE_READY"),
         QStringLiteral("secure_storage"),
         backend == QStringLiteral("unsupported-desktop-platform")
             ? QStringLiteral(
                   "No supported desktop credential vault is available; "
                   "refresh-token persistence is disabled.")
             : QStringLiteral("Desktop credential vault selected: %1.")
                   .arg(backend),
         {}, QJsonObject{{QStringLiteral("backend"), backend}},
         backend == QStringLiteral("unsupported-desktop-platform"));
}

bool AdminApplicationClient::configure(const QUrl &gatewayUrl,
                                       const QString &apiKey) {
  if (apiKey.trimmed().isEmpty()) {
    emitLocalError(QStringLiteral("INVALID_API_KEY"),
                   QStringLiteral("client.configure"),
                   QStringLiteral("An Admin API key is required."));
    return false;
  }
  if (!m_client->setGatewayUrl(gatewayUrl)) {
    return false;
  }
  m_client->setApiKey(apiKey);
  m_client->auth()->restoreSession();
  record(DiagnosticSeverity::Info, DiagnosticSource::Configuration,
         QStringLiteral("CONFIGURED"), QStringLiteral("client.configure"),
         QStringLiteral("Admin gateway configuration applied."));
  return true;
}

bool AdminApplicationClient::importHscfg(const QByteArray &data,
                                         const QString &pin) {
  const bool success = m_client->importHscfg(data, pin);
  if (success) {
    m_client->auth()->restoreSession();
    record(DiagnosticSeverity::Info, DiagnosticSource::Configuration,
           QStringLiteral("HSCFG_IMPORTED"),
           QStringLiteral("client.importHscfg"),
           QStringLiteral("Admin .hscfg configuration imported."));
  }
  return success;
}

bool AdminApplicationClient::importHscfgFile(const QString &path,
                                             const QString &pin) {
  const bool success = m_client->importHscfgFile(path, pin);
  if (success) {
    m_client->auth()->restoreSession();
    record(DiagnosticSeverity::Info, DiagnosticSource::Configuration,
           QStringLiteral("HSCFG_IMPORTED"),
           QStringLiteral("client.importHscfg"),
           QStringLiteral("Admin .hscfg configuration imported."));
  }
  return success;
}

void AdminApplicationClient::clearConfiguration() {
  m_refreshTimer.stop();
  m_preAuthToken.clear();
  m_client->clearImportedConfig();
  record(DiagnosticSeverity::Info, DiagnosticSource::Configuration,
         QStringLiteral("CLEARED"), QStringLiteral("client.clear"),
         QStringLiteral("Admin configuration cleared."));
}

bool AdminApplicationClient::isConfigured() const {
  return m_client->isConfigured();
}

bool AdminApplicationClient::hasImportedConfig() const {
  return m_client->hasImportedConfig();
}

void AdminApplicationClient::setHscfgTrustedEd25519PublicKey(
    const QByteArray &publicKey) {
  m_client->setHscfgTrustedEd25519PublicKey(publicKey);
}

void AdminApplicationClient::setHscfgRequireFullIntegrity(bool required) {
  m_client->setHscfgRequireFullIntegrity(required);
}

void AdminApplicationClient::signIn(const QString &username,
                                    const QString &password,
                                    const QJsonObject &deviceInfo) {
  m_client->auth()->login(username, password, deviceInfo);
}

void AdminApplicationClient::verifyTwoFactor(const QString &code,
                                             const QString &recoveryCode,
                                             const QJsonObject &deviceInfo) {
  if (m_preAuthToken.isEmpty()) {
    emitLocalError(QStringLiteral("TWO_FACTOR_NOT_REQUESTED"),
                   QStringLiteral("auth.2fa.verify"),
                   QStringLiteral("No pending two-factor challenge exists."));
    return;
  }
  m_client->auth()->verifyTwoFactor(m_preAuthToken, code, recoveryCode,
                                    deviceInfo);
}

void AdminApplicationClient::refreshSession() { m_client->auth()->refresh(); }

void AdminApplicationClient::signOut() {
  m_refreshTimer.stop();
  m_client->auth()->logout();
}

bool AdminApplicationClient::isAuthenticated() const {
  return m_client->auth()->isAuthenticated();
}

AdminState AdminApplicationClient::state() const { return m_client->state(); }

AdminUser AdminApplicationClient::currentUser() const {
  return m_client->auth()->currentUser();
}

RelayRealtimeClient *AdminApplicationClient::realtime() const {
  return m_client->relayRealtime();
}

void AdminApplicationClient::setAutoConnectRealtime(bool enabled) {
  m_autoConnectRealtime = enabled;
}

bool AdminApplicationClient::autoConnectRealtime() const {
  return m_autoConnectRealtime;
}

void AdminApplicationClient::connectRealtime() {
  if (!isAuthenticated()) {
    emitLocalError(QStringLiteral("AUTHENTICATION_REQUIRED"),
                   QStringLiteral("relay.connect"),
                   QStringLiteral("Sign in before connecting realtime."));
    return;
  }
  if (!m_client->relay()->isConnected()) {
    m_client->relay()->connectToServer();
  }
}

void AdminApplicationClient::disconnectRealtime(const QString &reason) {
  m_client->relay()->disconnectFromServer(reason);
}

bool AdminApplicationClient::isRealtimeConnected() const {
  return m_client->relay()->isConnected();
}

SystemClient *AdminApplicationClient::system() const {
  return m_client->system();
}

CameraClient *AdminApplicationClient::cameras() const {
  return m_client->cameras();
}

LiveClient *AdminApplicationClient::live() const { return m_client->live(); }

ArchiveClient *AdminApplicationClient::archive() const {
  return m_client->archive();
}

NotificationClient *AdminApplicationClient::notifications() const {
  return m_client->notifications();
}

QVector<SdkDiagnostic> AdminApplicationClient::diagnostics() const {
  return m_diagnostics;
}

SdkDiagnostic AdminApplicationClient::lastDiagnostic() const {
  return m_diagnostics.isEmpty() ? SdkDiagnostic{} : m_diagnostics.constLast();
}

void AdminApplicationClient::clearDiagnostics() { m_diagnostics.clear(); }

void AdminApplicationClient::setDiagnosticHistoryLimit(int limit) {
  m_diagnosticHistoryLimit = qMax(1, limit);
  while (m_diagnostics.size() > m_diagnosticHistoryLimit) {
    m_diagnostics.removeFirst();
  }
}

int AdminApplicationClient::diagnosticHistoryLimit() const {
  return m_diagnosticHistoryLimit;
}

void AdminApplicationClient::setDiagnosticLoggingEnabled(bool enabled) {
  m_diagnosticLoggingEnabled = enabled;
}

bool AdminApplicationClient::diagnosticLoggingEnabled() const {
  return m_diagnosticLoggingEnabled;
}

void AdminApplicationClient::record(
    DiagnosticSeverity severity, DiagnosticSource source, const QString &code,
    const QString &operation, const QString &message, const QString &requestId,
    const QJsonObject &details, bool retryable) {
  SdkDiagnostic diagnostic;
  diagnostic.timestamp = QDateTime::currentDateTimeUtc();
  diagnostic.severity = severity;
  diagnostic.source = source;
  diagnostic.code = code;
  diagnostic.operation = operation;
  diagnostic.message = sanitizeDiagnosticMessage(message);
  diagnostic.requestId = requestId;
  diagnostic.details = details;
  diagnostic.retryable = retryable;

  m_diagnostics.append(diagnostic);
  while (m_diagnostics.size() > m_diagnosticHistoryLimit) {
    m_diagnostics.removeFirst();
  }
  emit diagnosticOccurred(diagnostic);

  if (m_diagnosticLoggingEnabled) {
    const QByteArray serialized =
        QJsonDocument(diagnostic.toJson()).toJson(QJsonDocument::Compact);
    if (severity == DiagnosticSeverity::Error) {
      qCWarning(lcHubSightAdminSdk).noquote() << serialized;
    } else if (severity == DiagnosticSeverity::Warning) {
      qCWarning(lcHubSightAdminSdk).noquote() << serialized;
    } else {
      qCDebug(lcHubSightAdminSdk).noquote() << serialized;
    }
  }
}

void AdminApplicationClient::recordAdminError(const AdminError &error) {
  record(severityForAdminError(error), sourceForAdminError(error),
         error.serverCode.isEmpty() ? QStringLiteral("ADMIN_ERROR")
                                    : error.serverCode,
         error.operation, error.developerMessage, error.requestId,
         adminErrorDetails(error), error.retryable);
}

void AdminApplicationClient::emitLocalError(const QString &code,
                                            const QString &operation,
                                            const QString &message) {
  AdminError error;
  error.category = ErrorCategory::Validation;
  error.serverCode = code;
  error.developerMessage = message;
  error.operation = operation;
  recordAdminError(error);
  emit errorOccurred(error);
}

void AdminApplicationClient::handleTwoFactorRequired(
    const QString &preAuthToken) {
  m_refreshTimer.stop();
  m_preAuthToken = preAuthToken;
  record(DiagnosticSeverity::Info, DiagnosticSource::Authentication,
         QStringLiteral("TWO_FACTOR_REQUIRED"), QStringLiteral("auth.login"),
         QStringLiteral("Admin login requires two-factor verification."));
  emit twoFactorRequired();
}

void AdminApplicationClient::handleAuthenticated(const TokenSet &tokens,
                                                 const AdminUser &user) {
  m_preAuthToken.clear();
  scheduleTokenRefresh(tokens);
  record(DiagnosticSeverity::Info, DiagnosticSource::Authentication,
         QStringLiteral("AUTHENTICATED"), QStringLiteral("auth.login"),
         QStringLiteral("Admin authentication completed."));
  emit authenticated(user);
  if (m_autoConnectRealtime) {
    connectRealtime();
  }
}

void AdminApplicationClient::scheduleTokenRefresh(const TokenSet &tokens) {
  m_refreshTimer.stop();
  if (tokens.expiresInSeconds <= 0) {
    record(DiagnosticSeverity::Warning, DiagnosticSource::Authentication,
           QStringLiteral("TOKEN_EXPIRY_UNKNOWN"), QStringLiteral("auth"),
           QStringLiteral(
               "The authentication response did not include expires_in; "
               "automatic JWT refresh is disabled."),
           {}, {}, true);
    return;
  }

  const qint64 lifetimeMs =
      qMax<qint64>(1000, static_cast<qint64>(tokens.expiresInSeconds) * 1000);
  const qint64 safetyMs =
      qMax<qint64>(5000, qMin<qint64>(60000, lifetimeMs / 5));
  const qint64 refreshMs = qMax<qint64>(1000, lifetimeMs - safetyMs);
  m_refreshTimer.start(static_cast<int>(qMin<qint64>(refreshMs, INT_MAX)));
}

} // namespace HubSight::Admin
