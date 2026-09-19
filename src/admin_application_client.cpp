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
  m_liveHeartbeatTimer.setInterval(m_liveHeartbeatIntervalMs);
  connect(&m_liveHeartbeatTimer, &QTimer::timeout, this,
          [this]() { heartbeatLiveSessions(); });
  m_liveTeardownTimer.setSingleShot(true);
  connect(&m_liveTeardownTimer, &QTimer::timeout, this, [this]() {
    if (!m_pendingLiveTeardown) {
      return;
    }
    record(DiagnosticSeverity::Warning, DiagnosticSource::Lifecycle,
           QStringLiteral("LIVE_TEARDOWN_TIMEOUT"),
           QStringLiteral("live.teardown"),
           QStringLiteral(
               "Live release acknowledgements did not arrive before the "
               "teardown deadline; local sessions will be discarded."),
           {}, {}, true);
    m_liveReleaseInFlight.clear();
    m_liveReleasePending.clear();
    m_liveReleaseRetryScheduled.clear();
    m_liveReleaseAttempts.clear();
    clearLiveSessions(true);
    finishLiveTeardown();
  });

  connect(m_client.get(), &AdminClient::stateChanged, this,
          [this](AdminState state) {
            if (state == AdminState::Unauthenticated ||
                state == AdminState::Revoked) {
              m_preAuthToken.clear();
              m_refreshTimer.stop();
              if (m_pendingLiveTeardown) {
                m_liveReleaseInFlight.clear();
                m_liveReleasePending.clear();
                m_liveReleaseRetryScheduled.clear();
                m_liveReleaseAttempts.clear();
                clearLiveSessions(true);
                finishLiveTeardown();
              } else {
                clearLiveSessions(true);
              }
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
    clearLiveSessions(true);
    record(DiagnosticSeverity::Info, DiagnosticSource::Authentication,
           QStringLiteral("SIGNED_OUT"), QStringLiteral("auth.logout"),
           QStringLiteral("Admin session ended."));
    emit signedOut();
  });

  connect(m_client->live(), &LiveClient::sessionNegotiated, this,
          [this](const LiveSession &session) {
            handleLiveSessionNegotiated(session);
          });
  connect(m_client->live(), &LiveClient::sessionProfileChanged, this,
          [this](const LiveSession &session) {
            handleLiveSessionProfileChanged(session);
          });
  connect(m_client->live(), &LiveClient::sessionReleased, this,
          [this](const QString &sessionId) {
            handleLiveSessionReleased(sessionId);
          });
  connect(m_client->live(), &LiveClient::sessionHeartbeatReceived, this,
          [this](const QString &sessionId, const QJsonObject &) {
            handleLiveHeartbeat(sessionId);
          });
  connect(m_client->live(), &LiveClient::errorOccurred, this,
          [this](const AdminError &error) { handleLiveError(error); });

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
  if (m_pendingLiveTeardown) {
    emitLocalError(QStringLiteral("LIVE_TEARDOWN_IN_PROGRESS"),
                   QStringLiteral("client.configure"),
                   QStringLiteral("Wait for the active live teardown to "
                                  "complete before reconfiguring the client."));
    emit configurationApplied(false);
    return false;
  }
  if (apiKey.trimmed().isEmpty()) {
    emitLocalError(QStringLiteral("INVALID_API_KEY"),
                   QStringLiteral("client.configure"),
                   QStringLiteral("An Admin API key is required."));
    return false;
  }
  if (beginLiveTeardown(
          [this, gatewayUrl, apiKey]() { configureNow(gatewayUrl, apiKey); })) {
    return true;
  }
  return configureNow(gatewayUrl, apiKey);
}

bool AdminApplicationClient::importHscfg(const QByteArray &data,
                                         const QString &pin) {
  if (m_pendingLiveTeardown) {
    emitLocalError(QStringLiteral("LIVE_TEARDOWN_IN_PROGRESS"),
                   QStringLiteral("client.importHscfg"),
                   QStringLiteral("Wait for the active live teardown to "
                                  "complete before importing .hscfg."));
    emit hscfgImportCompleted(false);
    return false;
  }
  if (beginLiveTeardown([this, data, pin]() { importHscfgNow(data, pin); })) {
    return true;
  }
  return importHscfgNow(data, pin);
}

bool AdminApplicationClient::importHscfgFile(const QString &path,
                                             const QString &pin) {
  if (m_pendingLiveTeardown) {
    emitLocalError(QStringLiteral("LIVE_TEARDOWN_IN_PROGRESS"),
                   QStringLiteral("client.importHscfg"),
                   QStringLiteral("Wait for the active live teardown to "
                                  "complete before importing .hscfg."));
    emit hscfgImportCompleted(false);
    return false;
  }
  if (beginLiveTeardown(
          [this, path, pin]() { importHscfgFileNow(path, pin); })) {
    return true;
  }
  return importHscfgFileNow(path, pin);
}

void AdminApplicationClient::clearConfiguration() {
  if (beginLiveTeardown([this]() { clearConfigurationNow(); })) {
    return;
  }
  clearConfigurationNow();
}

bool AdminApplicationClient::configureNow(const QUrl &gatewayUrl,
                                          const QString &apiKey) {
  clearLiveSessions(true);
  if (!m_client->setGatewayUrl(gatewayUrl)) {
    emit configurationApplied(false);
    return false;
  }
  m_client->setApiKey(apiKey);
  m_client->auth()->restoreSession();
  record(DiagnosticSeverity::Info, DiagnosticSource::Configuration,
         QStringLiteral("CONFIGURED"), QStringLiteral("client.configure"),
         QStringLiteral("Admin gateway configuration applied."));
  emit configurationApplied(true);
  return true;
}

bool AdminApplicationClient::importHscfgNow(const QByteArray &data,
                                            const QString &pin) {
  clearLiveSessions(true);
  const bool success = m_client->importHscfg(data, pin);
  if (success) {
    m_client->auth()->restoreSession();
    record(DiagnosticSeverity::Info, DiagnosticSource::Configuration,
           QStringLiteral("HSCFG_IMPORTED"),
           QStringLiteral("client.importHscfg"),
           QStringLiteral("Admin .hscfg configuration imported."));
  }
  emit hscfgImportCompleted(success);
  return success;
}

bool AdminApplicationClient::importHscfgFileNow(const QString &path,
                                                const QString &pin) {
  clearLiveSessions(true);
  const bool success = m_client->importHscfgFile(path, pin);
  if (success) {
    m_client->auth()->restoreSession();
    record(DiagnosticSeverity::Info, DiagnosticSource::Configuration,
           QStringLiteral("HSCFG_IMPORTED"),
           QStringLiteral("client.importHscfg"),
           QStringLiteral("Admin .hscfg configuration imported."));
  }
  emit hscfgImportCompleted(success);
  return success;
}

void AdminApplicationClient::clearConfigurationNow() {
  clearLiveSessions(true);
  m_refreshTimer.stop();
  m_preAuthToken.clear();
  m_client->clearImportedConfig();
  record(DiagnosticSeverity::Info, DiagnosticSource::Configuration,
         QStringLiteral("CLEARED"), QStringLiteral("client.clear"),
         QStringLiteral("Admin configuration cleared."));
  emit configurationCleared();
}

void AdminApplicationClient::signOutNow() {
  m_refreshTimer.stop();
  m_client->auth()->logout();
}

bool AdminApplicationClient::beginLiveTeardown(
    std::function<void()> continuation) {
  if (m_pendingLiveTeardown) {
    emitLocalError(
        QStringLiteral("LIVE_TEARDOWN_IN_PROGRESS"),
        QStringLiteral("live.teardown"),
        QStringLiteral("A live teardown is already waiting to complete; "
                       "the requested lifecycle operation was not queued."));
    return true;
  }
  if (m_liveSessions.isEmpty() && m_liveReleasePending.isEmpty() &&
      m_liveStartingCameras.isEmpty()) {
    return false;
  }

  m_pendingLiveTeardown = std::move(continuation);
  for (const QString &cameraId : std::as_const(m_liveStartingCameras)) {
    m_liveCanceledStartingCameras.insert(cameraId);
  }
  releaseLiveSessions();
  if (m_pendingLiveTeardown && m_liveReleasePending.isEmpty() &&
      m_liveStartingCameras.isEmpty()) {
    finishLiveTeardown();
  }
  if (m_pendingLiveTeardown) {
    m_liveTeardownTimer.start(5000);
  }
  return true;
}

void AdminApplicationClient::finishLiveTeardown() {
  if (!m_pendingLiveTeardown || !m_liveReleasePending.isEmpty() ||
      !m_liveStartingCameras.isEmpty()) {
    return;
  }
  m_liveTeardownTimer.stop();
  auto continuation = std::move(m_pendingLiveTeardown);
  clearLiveSessions(true);
  continuation();
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
  if (beginLiveTeardown([this]() { signOutNow(); })) {
    return;
  }
  signOutNow();
}

bool AdminApplicationClient::isAuthenticated() const {
  return m_client->auth()->isAuthenticated();
}

AdminState AdminApplicationClient::state() const { return m_client->state(); }

AdminUser AdminApplicationClient::currentUser() const {
  return m_client->auth()->currentUser();
}

void AdminApplicationClient::startLive(const QString &cameraId,
                                       const QString &profile,
                                       const QJsonObject &options) {
  const QString normalizedCameraId = cameraId.trimmed();
  if (normalizedCameraId.isEmpty()) {
    AdminError error;
    error.category = ErrorCategory::Validation;
    error.serverCode = QStringLiteral("INVALID_INPUT");
    error.developerMessage = QStringLiteral("Camera ID is required.");
    error.operation = QStringLiteral("live.start");
    sendLiveError({}, error);
    return;
  }
  if (!isAuthenticated()) {
    AdminError error;
    error.category = ErrorCategory::Authentication;
    error.serverCode = QStringLiteral("AUTHENTICATION_REQUIRED");
    error.developerMessage =
        QStringLiteral("Sign in before starting a live session.");
    error.operation = QStringLiteral("live.start");
    sendLiveError({}, error);
    return;
  }
  if (m_liveCanceledStartingCameras.contains(normalizedCameraId)) {
    AdminError error;
    error.category = ErrorCategory::Conflict;
    error.serverCode = QStringLiteral("LIVE_START_CANCEL_PENDING");
    error.developerMessage = QStringLiteral(
        "A previous live negotiation for this camera is still being canceled.");
    error.operation = QStringLiteral("live.start");
    sendLiveError({}, error);
    return;
  }
  if (m_pendingLiveTeardown) {
    AdminError error;
    error.category = ErrorCategory::Conflict;
    error.serverCode = QStringLiteral("LIVE_TEARDOWN_IN_PROGRESS");
    error.developerMessage = QStringLiteral(
        "A live teardown is in progress; start a new session after it "
        "completes.");
    error.operation = QStringLiteral("live.start");
    sendLiveError({}, error);
    return;
  }
  for (auto it = m_liveSessions.cbegin(); it != m_liveSessions.cend(); ++it) {
    if (it.value().cameraId == normalizedCameraId) {
      AdminError error;
      error.category = ErrorCategory::Conflict;
      error.serverCode = QStringLiteral("LIVE_SESSION_ALREADY_ACTIVE");
      error.developerMessage =
          QStringLiteral("A live session is already active for this camera.");
      error.operation = QStringLiteral("live.start");
      error.details = QJsonObject{
          {QStringLiteral("camera_id"), normalizedCameraId},
          {QStringLiteral("session_id"), it.key()},
      };
      sendLiveError(it.key(), error);
      return;
    }
  }
  if (m_liveStartingCameras.contains(normalizedCameraId)) {
    AdminError error;
    error.category = ErrorCategory::Conflict;
    error.serverCode = QStringLiteral("LIVE_START_IN_PROGRESS");
    error.developerMessage = QStringLiteral(
        "A live session negotiation is already in progress for this camera.");
    error.operation = QStringLiteral("live.start");
    error.details = QJsonObject{
        {QStringLiteral("camera_id"), normalizedCameraId},
    };
    sendLiveError({}, error);
    return;
  }

  m_liveStartingCameras.insert(normalizedCameraId);
  emit liveStarting(normalizedCameraId);
  m_client->live()->negotiate(normalizedCameraId, profile, options);
}

void AdminApplicationClient::stopLive(const QString &sessionId) {
  const QString normalizedSessionId = sessionId.trimmed();
  if (normalizedSessionId.isEmpty()) {
    AdminError error;
    error.category = ErrorCategory::Validation;
    error.serverCode = QStringLiteral("INVALID_INPUT");
    error.developerMessage = QStringLiteral("Session ID is required.");
    error.operation = QStringLiteral("live.stop");
    sendLiveError({}, error);
    return;
  }
  if (!m_liveSessions.contains(normalizedSessionId)) {
    AdminError error;
    error.category = ErrorCategory::Validation;
    error.serverCode = QStringLiteral("LIVE_SESSION_NOT_FOUND");
    error.developerMessage =
        QStringLiteral("The requested live session is not managed by this "
                       "application client.");
    error.operation = QStringLiteral("live.stop");
    error.details = QJsonObject{
        {QStringLiteral("session_id"), normalizedSessionId},
    };
    sendLiveError(normalizedSessionId, error);
    return;
  }
  if (m_liveReleasePending.contains(normalizedSessionId)) {
    return;
  }

  requestLiveRelease(normalizedSessionId);
}

void AdminApplicationClient::stopAllLive() {
  // LiveClient intentionally keeps its public negotiate API asynchronous and
  // does not expose cancellation tokens. Mark in-flight starts as canceled so
  // a late response is released instead of being promoted to a new session.
  for (const QString &cameraId : std::as_const(m_liveStartingCameras)) {
    m_liveCanceledStartingCameras.insert(cameraId);
  }
  const QStringList ids = liveSessionIds();
  for (const QString &sessionId : ids) {
    stopLive(sessionId);
  }
}

void AdminApplicationClient::changeLiveProfile(const QString &sessionId,
                                               const QString &profile,
                                               const QJsonObject &options) {
  const QString normalizedSessionId = sessionId.trimmed();
  const QString normalizedProfile = profile.trimmed();
  if (normalizedSessionId.isEmpty() || normalizedProfile.isEmpty()) {
    AdminError error;
    error.category = ErrorCategory::Validation;
    error.serverCode = QStringLiteral("INVALID_INPUT");
    error.developerMessage =
        QStringLiteral("Session ID and profile are required.");
    error.operation = QStringLiteral("live.changeProfile");
    sendLiveError(normalizedSessionId, error);
    return;
  }
  if (!m_liveSessions.contains(normalizedSessionId)) {
    AdminError error;
    error.category = ErrorCategory::Validation;
    error.serverCode = QStringLiteral("LIVE_SESSION_NOT_FOUND");
    error.developerMessage =
        QStringLiteral("The requested live session is not managed by this "
                       "application client.");
    error.operation = QStringLiteral("live.changeProfile");
    error.details = QJsonObject{
        {QStringLiteral("session_id"), normalizedSessionId},
    };
    sendLiveError(normalizedSessionId, error);
    return;
  }
  m_client->live()->changeProfile(normalizedSessionId, normalizedProfile,
                                  options);
}

void AdminApplicationClient::reportLiveQoe(const QString &sessionId,
                                           const QJsonObject &qoe) {
  const QString normalizedSessionId = sessionId.trimmed();
  if (normalizedSessionId.isEmpty() || qoe.isEmpty()) {
    AdminError error;
    error.category = ErrorCategory::Validation;
    error.serverCode = QStringLiteral("INVALID_INPUT");
    error.developerMessage =
        QStringLiteral("Session ID and QoE metrics are required.");
    error.operation = QStringLiteral("live.reportQoe");
    sendLiveError(normalizedSessionId, error);
    return;
  }
  if (!m_liveSessions.contains(normalizedSessionId)) {
    AdminError error;
    error.category = ErrorCategory::Validation;
    error.serverCode = QStringLiteral("LIVE_SESSION_NOT_FOUND");
    error.developerMessage =
        QStringLiteral("The requested live session is not managed by this "
                       "application client.");
    error.operation = QStringLiteral("live.reportQoe");
    error.details = QJsonObject{
        {QStringLiteral("session_id"), normalizedSessionId},
    };
    sendLiveError(normalizedSessionId, error);
    return;
  }
  m_client->live()->reportQoe(normalizedSessionId, qoe);
}

LiveSession
AdminApplicationClient::liveSession(const QString &sessionId) const {
  return m_liveSessions.value(sessionId.trimmed());
}

QStringList AdminApplicationClient::liveSessionIds() const {
  QStringList ids = m_liveSessions.keys();
  ids.sort();
  return ids;
}

void AdminApplicationClient::setLiveHeartbeatInterval(int intervalMs) {
  m_liveHeartbeatIntervalMs = qBound(1000, intervalMs, 300000);
  m_liveHeartbeatTimer.setInterval(m_liveHeartbeatIntervalMs);
  if (!m_liveSessions.isEmpty()) {
    m_liveHeartbeatTimer.start();
  }
}

int AdminApplicationClient::liveHeartbeatInterval() const {
  return m_liveHeartbeatIntervalMs;
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

AccountClient *AdminApplicationClient::account() const {
  return m_client->account();
}

SystemClient *AdminApplicationClient::system() const {
  return m_client->system();
}

SystemOperationsClient *AdminApplicationClient::systemOperations() const {
  return m_client->systemOperations();
}

CameraClient *AdminApplicationClient::cameras() const {
  return m_client->cameras();
}

CameraManagementClient *AdminApplicationClient::cameraManagement() const {
  return m_client->cameraManagement();
}

IdentityClient *AdminApplicationClient::identity() const {
  return m_client->identity();
}

IntegrationClient *AdminApplicationClient::integrations() const {
  return m_client->integrations();
}

MemberClient *AdminApplicationClient::members() const {
  return m_client->members();
}

LiveClient *AdminApplicationClient::live() const { return m_client->live(); }

ArchiveClient *AdminApplicationClient::archive() const {
  return m_client->archive();
}

NotificationClient *AdminApplicationClient::notifications() const {
  return m_client->notifications();
}

AdminEndpointClient *AdminApplicationClient::api() const {
  return m_client->api();
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

void AdminApplicationClient::handleLiveSessionNegotiated(
    const LiveSession &session) {
  if (session.sessionId.trimmed().isEmpty()) {
    return;
  }
  const QString cameraId = session.cameraId.trimmed();
  const bool expectedStart =
      !cameraId.isEmpty() && m_liveStartingCameras.contains(cameraId);
  if (!cameraId.isEmpty() && m_liveCanceledStartingCameras.contains(cameraId)) {
    m_liveCanceledStartingCameras.remove(cameraId);
    m_liveStartingCameras.remove(cameraId);
    record(DiagnosticSeverity::Warning, DiagnosticSource::Lifecycle,
           QStringLiteral("LIVE_NEGOTIATION_CANCELED_LATE"),
           QStringLiteral("live.start"),
           QStringLiteral(
               "A canceled live negotiation completed late; its server "
               "session will be released without exposing it to the app."),
           {}, QJsonObject{{QStringLiteral("session_id"), session.sessionId}},
           true);
    requestLiveRelease(session.sessionId);
    return;
  }
  if (!cameraId.isEmpty()) {
    m_liveStartingCameras.remove(cameraId);
  }

  const QString sessionId = session.sessionId.trimmed();
  const bool existed = m_liveSessions.contains(sessionId);
  if (!existed && !expectedStart) {
    record(DiagnosticSeverity::Warning, DiagnosticSource::Lifecycle,
           QStringLiteral("STALE_LIVE_NEGOTIATION"),
           QStringLiteral("live.start"),
           QStringLiteral(
               "Ignoring a live negotiation response that no longer belongs "
               "to an active application request."),
           {}, QJsonObject{{QStringLiteral("session_id"), sessionId}}, true);
    requestLiveRelease(sessionId);
    return;
  }
  m_liveSessions.insert(sessionId, session);
  if (!m_liveHeartbeatTimer.isActive()) {
    m_liveHeartbeatTimer.start();
  }

  if (existed) {
    emit liveSessionChanged(session);
  } else {
    emit liveSessionStarted(session);
  }

  // The core SDK intentionally has no bundled media engine. Keep the
  // signaling/session reservation useful while reporting the exact capability
  // gap instead of pretending that media has connected.
  reportWebRtcBackendUnavailable(session);
}

void AdminApplicationClient::handleLiveSessionProfileChanged(
    const LiveSession &session) {
  const QString sessionId = session.sessionId.trimmed();
  if (sessionId.isEmpty()) {
    return;
  }
  if (m_liveReleasePending.contains(sessionId) || m_pendingLiveTeardown) {
    return;
  }
  const auto existing = m_liveSessions.constFind(sessionId);
  if (existing == m_liveSessions.constEnd()) {
    return;
  }

  LiveSession updated = session;
  if (updated.cameraId.isEmpty()) {
    updated.cameraId = existing->cameraId;
  }
  if (updated.profile.isEmpty()) {
    updated.profile = existing->profile;
  }
  if (updated.mediaTransport.isEmpty()) {
    updated.mediaTransport = existing->mediaTransport;
  }
  if (updated.state.isEmpty()) {
    updated.state = existing->state;
  }
  if (!updated.expiresAt.isValid() && existing->expiresAt.isValid()) {
    updated.expiresAt = existing->expiresAt;
  }
  if (updated.webRtcConfiguration.iceServers.isEmpty()) {
    updated.webRtcConfiguration = existing->webRtcConfiguration;
  }
  if (!updated.hasRemoteDescription && existing->hasRemoteDescription) {
    updated.remoteDescription = existing->remoteDescription;
    updated.hasRemoteDescription = true;
  }
  if (updated.remoteCandidates.isEmpty()) {
    updated.remoteCandidates = existing->remoteCandidates;
  }
  if (updated.signaling.isEmpty()) {
    updated.signaling = existing->signaling;
  }
  m_liveSessions.insert(sessionId, updated);
  emit liveSessionChanged(updated);
}

void AdminApplicationClient::handleLiveSessionReleased(
    const QString &sessionId) {
  const QString normalizedSessionId = sessionId.trimmed();
  m_liveReleaseInFlight.remove(normalizedSessionId);
  m_liveReleasePending.remove(normalizedSessionId);
  m_liveReleaseRetryScheduled.remove(normalizedSessionId);
  m_liveReleaseAttempts.remove(normalizedSessionId);
  m_liveHeartbeatInFlight.remove(normalizedSessionId);
  const bool wasManaged = m_liveSessions.remove(normalizedSessionId) > 0;
  m_client->webrtc()->closePeerConnection(normalizedSessionId);
  if (wasManaged) {
    emit liveSessionStopped(normalizedSessionId);
  }
  if (m_liveSessions.isEmpty()) {
    m_liveHeartbeatTimer.stop();
  }
  finishLiveTeardown();
}

void AdminApplicationClient::handleLiveHeartbeat(const QString &sessionId) {
  m_liveHeartbeatInFlight.remove(sessionId.trimmed());
}

void AdminApplicationClient::handleLiveError(const AdminError &error) {
  const QJsonObject details = error.details.toObject();
  const QString sessionId =
      details.value(QStringLiteral("session_id")).toString();
  const QString cameraId =
      details.value(QStringLiteral("camera_id")).toString();
  if (!cameraId.isEmpty() &&
      error.operation == QStringLiteral("live.sessions.negotiate")) {
    m_liveStartingCameras.remove(cameraId);
    m_liveCanceledStartingCameras.remove(cameraId);
    finishLiveTeardown();
  }
  if (!sessionId.isEmpty() &&
      error.operation == QStringLiteral("live.sessions.heartbeat")) {
    m_liveHeartbeatInFlight.remove(sessionId);
    if (!error.retryable) {
      const bool wasManaged = m_liveSessions.remove(sessionId) > 0;
      m_client->webrtc()->closePeerConnection(sessionId);
      if (wasManaged) {
        emit liveSessionStopped(sessionId);
      }
      if (m_liveSessions.isEmpty()) {
        m_liveHeartbeatTimer.stop();
      }
    }
  }
  if (!sessionId.isEmpty() &&
      error.operation == QStringLiteral("live.sessions.release")) {
    m_liveReleaseInFlight.remove(sessionId);
    if (error.retryable && m_liveReleaseAttempts.value(sessionId) < 3 &&
        isAuthenticated()) {
      if (!m_liveReleaseRetryScheduled.contains(sessionId)) {
        m_liveReleaseRetryScheduled.insert(sessionId);
        QTimer::singleShot(250, this, [this, sessionId]() {
          m_liveReleaseRetryScheduled.remove(sessionId);
          retryLiveRelease(sessionId);
        });
      }
    } else if (!error.retryable || !m_pendingLiveTeardown) {
      m_liveReleasePending.remove(sessionId);
      m_liveReleaseAttempts.remove(sessionId);
    }
  }
  emit liveError(sessionId, error);
  if (!sessionId.isEmpty() &&
      error.operation == QStringLiteral("live.sessions.release")) {
    finishLiveTeardown();
  }
}

void AdminApplicationClient::sendLiveError(const QString &sessionId,
                                           const AdminError &error,
                                           bool recordDiagnostic,
                                           DiagnosticSource source) {
  if (recordDiagnostic) {
    if (source == DiagnosticSource::Http) {
      recordAdminError(error);
    } else {
      record(error.retryable ? DiagnosticSeverity::Warning
                             : DiagnosticSeverity::Error,
             source,
             error.serverCode.isEmpty() ? QStringLiteral("LIVE_ERROR")
                                        : error.serverCode,
             error.operation, error.developerMessage, error.requestId,
             error.details.toObject(), error.retryable);
    }
  }
  emit errorOccurred(error);
  emit liveError(sessionId, error);
}

void AdminApplicationClient::reportWebRtcBackendUnavailable(
    const LiveSession &session) {
  const QString mediaTransport = session.mediaTransport.trimmed();
  const bool usesWebRtc =
      mediaTransport.compare(QStringLiteral("webrtc"), Qt::CaseInsensitive) ==
          0 ||
      (mediaTransport.isEmpty() && session.hasRemoteDescription);
  if (!usesWebRtc) {
    return;
  }
  const WebRtcPeerConnection *peer =
      m_client->webrtc()->peerConnection(session.sessionId);
  if (peer && peer->hasBackend()) {
    return;
  }

  AdminError error;
  error.category = ErrorCategory::Configuration;
  error.serverCode = QStringLiteral("WEBRTC_BACKEND_UNAVAILABLE");
  error.developerMessage = QStringLiteral(
      "Live signaling succeeded, but no native WebRTC media backend is "
      "installed. The session remains available for an application-provided "
      "media adapter.");
  error.operation = QStringLiteral("live.start");
  error.retryable = false;
  error.details = QJsonObject{
      {QStringLiteral("session_id"), session.sessionId},
      {QStringLiteral("camera_id"), session.cameraId},
      {QStringLiteral("media_transport"), session.mediaTransport},
  };
  sendLiveError(session.sessionId, error, true, DiagnosticSource::WebRtc);
}

void AdminApplicationClient::heartbeatLiveSessions() {
  if (m_liveSessions.isEmpty()) {
    m_liveHeartbeatTimer.stop();
    return;
  }
  if (!isAuthenticated()) {
    return;
  }

  const QDateTime now = QDateTime::currentDateTimeUtc();
  const QStringList ids = liveSessionIds();
  for (const QString &sessionId : ids) {
    const LiveSession session = m_liveSessions.value(sessionId);
    if (session.expiresAt.isValid() && session.expiresAt <= now) {
      stopLive(sessionId);
      continue;
    }
    if (m_liveHeartbeatInFlight.contains(sessionId) ||
        m_liveReleasePending.contains(sessionId)) {
      continue;
    }
    m_liveHeartbeatInFlight.insert(sessionId);
    m_client->live()->heartbeat(sessionId);
  }
}

void AdminApplicationClient::clearLiveSessions(bool emitStopped) {
  const QStringList ids = liveSessionIds();
  m_liveSessions.clear();
  m_liveStartingCameras.clear();
  m_liveCanceledStartingCameras.clear();
  m_liveHeartbeatInFlight.clear();
  m_liveReleaseInFlight.clear();
  m_liveReleasePending.clear();
  m_liveReleaseRetryScheduled.clear();
  m_liveReleaseAttempts.clear();
  m_liveHeartbeatTimer.stop();
  for (const QString &sessionId : ids) {
    m_client->webrtc()->closePeerConnection(sessionId);
    if (emitStopped) {
      emit liveSessionStopped(sessionId);
    }
  }
}

void AdminApplicationClient::releaseLiveSessions() {
  const QStringList ids = liveSessionIds();
  for (const QString &sessionId : ids) {
    requestLiveRelease(sessionId);
  }
}

void AdminApplicationClient::requestLiveRelease(const QString &sessionId) {
  const QString normalizedSessionId = sessionId.trimmed();
  if (normalizedSessionId.isEmpty()) {
    return;
  }
  m_liveReleasePending.insert(normalizedSessionId);
  if (m_liveReleaseInFlight.contains(normalizedSessionId) ||
      m_liveReleaseRetryScheduled.contains(normalizedSessionId)) {
    return;
  }
  if (!isAuthenticated()) {
    m_liveReleasePending.remove(normalizedSessionId);
    m_liveReleaseAttempts.remove(normalizedSessionId);
    record(DiagnosticSeverity::Warning, DiagnosticSource::Lifecycle,
           QStringLiteral("LIVE_RELEASE_NOT_ATTEMPTED"),
           QStringLiteral("live.release"),
           QStringLiteral(
               "The live session could not be released because the Admin "
               "authentication session is no longer available."),
           {}, QJsonObject{{QStringLiteral("session_id"), normalizedSessionId}},
           true);
    finishLiveTeardown();
    return;
  }
  m_liveReleaseInFlight.insert(normalizedSessionId);
  m_liveReleaseAttempts[normalizedSessionId] =
      m_liveReleaseAttempts.value(normalizedSessionId) + 1;
  m_client->live()->release(normalizedSessionId);
}

void AdminApplicationClient::retryLiveRelease(const QString &sessionId) {
  const QString normalizedSessionId = sessionId.trimmed();
  if (!m_liveReleasePending.contains(normalizedSessionId) ||
      m_liveReleaseInFlight.contains(normalizedSessionId)) {
    return;
  }
  requestLiveRelease(normalizedSessionId);
}

} // namespace HubSight::Admin
