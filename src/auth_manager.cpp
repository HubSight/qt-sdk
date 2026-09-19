#include "../include/hubsight/admin/auth_manager.h"

#include "admin_transport.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>

namespace HubSight::Admin {
namespace {

constexpr auto kRefreshTokenKey = "hubsight.admin.refresh_token";

AdminError localError(ErrorCategory category, const QString &code,
                      const QString &message, const QString &operation) {
  AdminError error;
  error.category = category;
  error.serverCode = code;
  error.developerMessage = message;
  error.operation = operation;
  return error;
}

QByteArray jsonBody(const QJsonObject &object) {
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

} // namespace

AuthManager::AuthManager(AdminTransport *transport, SecureStoragePtr storage,
                         QObject *parent)
    : QObject(parent), m_transport(transport), m_storage(std::move(storage)) {
  if (!m_storage) {
    m_storage = std::make_shared<InMemorySecureStorage>();
  }
  connect(m_transport, &AdminTransport::allRequestsCanceled, this, [this]() {
    m_pending.clear();
    m_refreshInFlight = false;
  });
  connect(m_transport, &AdminTransport::finished, this,
          [this](quint64 requestId, const TransportResponse &response) {
            const auto pending = m_pending.find(requestId);
            if (pending == m_pending.end()) {
              return;
            }
            const PendingRequest pendingRequest = pending.value();
            m_pending.erase(pending);
            if (pendingRequest.generation != m_generation) {
              return;
            }
            const PendingKind kind = pendingRequest.kind;
            handleResponse(kind, response.body, response.httpStatus,
                           response.requestId, response.retryAfterSeconds,
                           static_cast<int>(response.networkError),
                           response.timedOut, response.networkErrorText);
          });
}

bool AuthManager::isAuthenticated() const { return !m_accessToken.isEmpty(); }

AdminState AuthManager::state() const { return m_state; }

AdminUser AuthManager::currentUser() const { return m_currentUser; }

void AuthManager::setSecureStorage(SecureStoragePtr storage) {
  m_storage =
      storage ? std::move(storage) : std::make_shared<InMemorySecureStorage>();
}

void AuthManager::invalidateSession() { clearLocalSession(); }

void AuthManager::setState(AdminState state) {
  if (m_state == state) {
    return;
  }
  m_state = state;
  emit stateChanged(m_state);
}

void AuthManager::emitError(const AdminError &error) {
  emit errorOccurred(error);
}

void AuthManager::login(const QString &username, const QString &password,
                        const QJsonObject &deviceInfo) {
  if (!m_transport->isConfigured()) {
    emitError(m_transport->configurationError(QStringLiteral("auth.login")));
    return;
  }
  if (username.trimmed().isEmpty() || password.isEmpty()) {
    emitError(localError(ErrorCategory::Validation,
                         QStringLiteral("INVALID_INPUT"),
                         QStringLiteral("Username and password are required."),
                         QStringLiteral("auth.login")));
    return;
  }

  QJsonObject payload{
      {QStringLiteral("username"), username},
      {QStringLiteral("password"), password},
  };
  if (!deviceInfo.isEmpty()) {
    payload.insert(QStringLiteral("device_info"), deviceInfo);
    payload.insert(QStringLiteral("device_name"),
                   deviceInfo.value(QStringLiteral("device_label")).toString());
    payload.insert(QStringLiteral("platform"),
                   deviceInfo.value(QStringLiteral("client_type")).toString());
    payload.insert(QStringLiteral("device_id"),
                   deviceInfo.value(QStringLiteral("fingerprint")).toString());
  }

  setState(AdminState::Authenticating);
  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/auth/login");
  request.operation = QStringLiteral("auth.login");
  request.body = jsonBody(payload);
  const quint64 requestId = m_transport->send(request);
  if (requestId == 0) {
    emitError(m_transport->configurationError(request.operation));
    setState(AdminState::Unauthenticated);
    return;
  }
  m_pending.insert(requestId, {PendingKind::Login, m_generation});
}

void AuthManager::verifyTwoFactor(const QString &preAuthToken,
                                  const QString &code,
                                  const QString &recoveryCode,
                                  const QJsonObject &deviceInfo) {
  if (!m_transport->isConfigured()) {
    emitError(
        m_transport->configurationError(QStringLiteral("auth.2fa.verify")));
    return;
  }
  if (preAuthToken.trimmed().isEmpty() ||
      (code.trimmed().isEmpty() && recoveryCode.trimmed().isEmpty())) {
    emitError(localError(
        ErrorCategory::Validation, QStringLiteral("INVALID_INPUT"),
        QStringLiteral(
            "A pre-auth token and a TOTP or recovery code are required."),
        QStringLiteral("auth.2fa.verify")));
    return;
  }

  QJsonObject payload{
      {QStringLiteral("pre_auth_token"), preAuthToken},
      {QStringLiteral("code"), code},
  };
  if (!recoveryCode.trimmed().isEmpty()) {
    payload.insert(QStringLiteral("recovery_code"), recoveryCode);
  }
  if (!deviceInfo.isEmpty()) {
    payload.insert(QStringLiteral("device_info"), deviceInfo);
  }

  setState(AdminState::Authenticating);
  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/auth/2fa/verify");
  request.operation = QStringLiteral("auth.2fa.verify");
  request.body = jsonBody(payload);
  const quint64 requestId = m_transport->send(request);
  if (requestId == 0) {
    emitError(m_transport->configurationError(request.operation));
    setState(AdminState::Unauthenticated);
    return;
  }
  m_pending.insert(requestId, {PendingKind::VerifyTwoFactor, m_generation});
}

void AuthManager::refresh() {
  if (m_refreshInFlight) {
    return;
  }
  if (!m_transport->isConfigured()) {
    emitError(m_transport->configurationError(QStringLiteral("auth.refresh")));
    return;
  }
  const auto stored = m_storage->read(QString::fromLatin1(kRefreshTokenKey));
  if (!stored || stored->isEmpty()) {
    const AdminError error = localError(
        ErrorCategory::Authentication, QStringLiteral("REFRESH_TOKEN_REQUIRED"),
        QStringLiteral("No Admin refresh token is available."),
        QStringLiteral("auth.refresh"));
    clearLocalSession(AdminState::Revoked);
    emitError(error);
    return;
  }

  QJsonObject payload{
      {QStringLiteral("refresh_token"), QString::fromUtf8(*stored)}};
  setState(AdminState::Authenticating);
  m_refreshInFlight = true;
  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/auth/refresh");
  request.operation = QStringLiteral("auth.refresh");
  request.body = jsonBody(payload);
  const quint64 requestId = m_transport->send(request);
  if (requestId == 0) {
    m_refreshInFlight = false;
    emitError(m_transport->configurationError(request.operation));
    return;
  }
  m_pending.insert(requestId, {PendingKind::Refresh, m_generation});
}

void AuthManager::logout() {
  if (!m_transport->isConfigured() || !isAuthenticated()) {
    clearLocalSession();
    emit loggedOut();
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/auth/logout");
  request.operation = QStringLiteral("auth.logout");
  request.authentication = RequestAuth::Protected;
  const quint64 requestId = m_transport->send(request);
  if (requestId == 0) {
    clearLocalSession();
    emit loggedOut();
    return;
  }
  m_pending.insert(requestId, {PendingKind::Logout, m_generation});
}

void AuthManager::fetchCurrentUser() {
  if (!m_transport->isConfigured() || !isAuthenticated()) {
    emitError(localError(
        ErrorCategory::Authentication,
        QStringLiteral("AUTHENTICATION_REQUIRED"),
        QStringLiteral("An authenticated Admin session is required."),
        QStringLiteral("auth.me")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = QStringLiteral("/auth/me");
  request.operation = QStringLiteral("auth.me");
  request.authentication = RequestAuth::Protected;
  const quint64 requestId = m_transport->send(request);
  if (requestId == 0) {
    emitError(m_transport->configurationError(request.operation));
    return;
  }
  m_pending.insert(requestId, {PendingKind::CurrentUser, m_generation});
}

void AuthManager::clearLocalSession(AdminState nextState) {
  ++m_generation;
  m_accessToken.clear();
  m_transport->setAccessToken({});
  m_storage->remove(QString::fromLatin1(kRefreshTokenKey));
  const bool hadUser = m_currentUser.isValid();
  m_currentUser = {};
  if (hadUser) {
    emit currentUserChanged(m_currentUser);
  }
  setState(nextState);
}

void AuthManager::completeTokenResponse(const QJsonObject &json,
                                        PendingKind kind,
                                        const QString &requestId) {
  Q_UNUSED(requestId)
  const TokenSet tokens = TokenSet::fromJson(json);
  if (!tokens.isValid()) {
    emitError(localError(
        ErrorCategory::Parse, QStringLiteral("INVALID_AUTH_RESPONSE"),
        QStringLiteral(
            "Admin authentication response did not contain an access token."),
        kind == PendingKind::Refresh ? QStringLiteral("auth.refresh")
                                     : QStringLiteral("auth.login")));
    setState(AdminState::Revoked);
    return;
  }

  if (!tokens.refreshToken.isEmpty() &&
      !m_storage->write(QString::fromLatin1(kRefreshTokenKey),
                        tokens.refreshToken.toUtf8())) {
    const QString operation = kind == PendingKind::Refresh
                                  ? QStringLiteral("auth.refresh")
                                  : QStringLiteral("auth.login");
    const AdminError error = localError(
        ErrorCategory::Configuration,
        QStringLiteral("SECURE_STORAGE_WRITE_FAILED"),
        QStringLiteral("Unable to persist the Admin refresh token securely."),
        operation);
    clearLocalSession(AdminState::Revoked);
    emitError(error);
    return;
  }

  m_accessToken = tokens.accessToken;
  m_transport->setAccessToken(m_accessToken);

  const QJsonObject userObject = json.value(QStringLiteral("user")).toObject();
  if (!userObject.isEmpty()) {
    m_currentUser = AdminUser::fromJson(userObject);
    emit currentUserChanged(m_currentUser);
  }
  setState(AdminState::Ready);
  if (kind == PendingKind::Refresh) {
    emit tokenRefreshed(tokens);
  } else {
    emit loginSucceeded(tokens, m_currentUser);
  }
}

void AuthManager::handleResponse(PendingKind kind, const QByteArray &body,
                                 int statusCode, const QString &requestIdHeader,
                                 int retryAfterSeconds, int networkErrorCode,
                                 bool timedOut,
                                 const QString &networkErrorText) {
  if (kind == PendingKind::Refresh) {
    m_refreshInFlight = false;
  }
  TransportResponse response;
  response.httpStatus = statusCode;
  response.body = body;
  response.requestId = requestIdHeader;
  response.retryAfterSeconds = retryAfterSeconds;
  response.networkError =
      static_cast<QNetworkReply::NetworkError>(networkErrorCode);
  response.timedOut = timedOut;
  response.networkErrorText = networkErrorText;

  const QString operation = [&]() {
    switch (kind) {
    case PendingKind::Login:
      return QStringLiteral("auth.login");
    case PendingKind::VerifyTwoFactor:
      return QStringLiteral("auth.2fa.verify");
    case PendingKind::Refresh:
      return QStringLiteral("auth.refresh");
    case PendingKind::Logout:
      return QStringLiteral("auth.logout");
    case PendingKind::CurrentUser:
      return QStringLiteral("auth.me");
    }
    return QStringLiteral("auth");
  }();

  if (!response.isHttpSuccess()) {
    const AdminError error = m_transport->errorFor(response, operation);
    if (kind == PendingKind::Logout) {
      clearLocalSession();
      emit loggedOut();
    } else if (kind == PendingKind::Refresh && error.isAuthenticationError()) {
      clearLocalSession(AdminState::Revoked);
    } else if (kind == PendingKind::CurrentUser &&
               error.isAuthenticationError()) {
      clearLocalSession(AdminState::Revoked);
    } else if ((kind == PendingKind::Login ||
                kind == PendingKind::VerifyTwoFactor) &&
               (error.isAuthenticationError() ||
                error.category == ErrorCategory::Validation ||
                error.category == ErrorCategory::Authorization)) {
      setState(AdminState::Unauthenticated);
    } else if (error.isMaintenance()) {
      setState(AdminState::Maintenance);
    } else if (error.isNetworkError()) {
      setState(AdminState::Offline);
    }
    emitError(error);
    return;
  }

  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
  if (kind == PendingKind::Logout) {
    clearLocalSession();
    emit loggedOut();
    return;
  }
  if (document.isNull() || !document.isObject()) {
    const AdminError error = localError(
        ErrorCategory::Parse, QStringLiteral("INVALID_JSON_RESPONSE"),
        QStringLiteral("Admin API returned invalid JSON."), operation);
    if (kind == PendingKind::Refresh) {
      setState(AdminState::Revoked);
    } else if (kind == PendingKind::Login ||
               kind == PendingKind::VerifyTwoFactor) {
      setState(AdminState::Unauthenticated);
    }
    emitError(error);
    return;
  }

  const QJsonObject json = document.object();
  if ((kind == PendingKind::Login || kind == PendingKind::VerifyTwoFactor) &&
      json.value(QStringLiteral("status")).toString() ==
          QStringLiteral("2fa_required")) {
    const QString preAuthToken =
        json.value(QStringLiteral("pre_auth_token")).toString();
    if (preAuthToken.isEmpty()) {
      emitError(localError(
          ErrorCategory::Parse, QStringLiteral("INVALID_2FA_RESPONSE"),
          QStringLiteral("Admin API requested 2FA without a pre-auth token."),
          operation));
      return;
    }
    setState(AdminState::Authenticating);
    emit twoFactorRequired(preAuthToken);
    return;
  }

  if (kind == PendingKind::Login || kind == PendingKind::VerifyTwoFactor ||
      kind == PendingKind::Refresh) {
    completeTokenResponse(json, kind, requestIdHeader);
    return;
  }

  const QJsonObject userObject = json.value(QStringLiteral("user")).toObject();
  const AdminUser user =
      AdminUser::fromJson(userObject.isEmpty() ? json : userObject);
  if (!user.isValid()) {
    emitError(localError(
        ErrorCategory::Parse, QStringLiteral("INVALID_USER_RESPONSE"),
        QStringLiteral("Admin API returned no current user."), operation));
    return;
  }
  m_currentUser = user;
  setState(AdminState::Ready);
  emit currentUserChanged(m_currentUser);
}

} // namespace HubSight::Admin
