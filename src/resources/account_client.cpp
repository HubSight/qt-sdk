#include "../../include/hubsight/admin/resources/account_client.h"

#include "../admin_transport.h"
#include "../resource_helpers.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QUrlQuery>

namespace HubSight::Admin {
namespace {

QByteArray jsonBody(const QJsonObject &object) {
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

AdminError invalidInput(const QString &operation, const QString &message) {
  AdminError error;
  error.category = ErrorCategory::Validation;
  error.serverCode = QStringLiteral("INVALID_INPUT");
  error.developerMessage = message;
  error.operation = operation;
  return error;
}

QString encodedPathPart(const QString &value) {
  return QString::fromUtf8(QUrl::toPercentEncoding(value.trimmed()));
}

QString passkeyPath(const QString &passkeyId) {
  return QStringLiteral("/auth/passkeys/") + encodedPathPart(passkeyId);
}

QString sessionPath(const QString &sessionId) {
  return QStringLiteral("/profile/sessions/") + encodedPathPart(sessionId);
}

bool decodeResponse(const QByteArray &body, QJsonObject *object) {
  if (body.trimmed().isEmpty()) {
    *object = {};
    return true;
  }
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
  if (document.isNull()) {
    return false;
  }
  if (document.isObject()) {
    *object = document.object();
    return true;
  }
  if (document.isArray()) {
    *object = QJsonObject{{QStringLiteral("items"), document.array()}};
    return true;
  }
  return false;
}

QJsonObject codeBody(const QString &code, const QString &recoveryCode = {}) {
  QJsonObject body;
  if (!code.trimmed().isEmpty()) {
    body.insert(QStringLiteral("code"), code.trimmed());
  }
  if (!recoveryCode.trimmed().isEmpty()) {
    body.insert(QStringLiteral("recovery_code"), recoveryCode.trimmed());
  }
  return body;
}

} // namespace

AccountClient::AccountClient(AdminTransport *transport, QObject *parent)
    : QObject(parent), m_transport(transport) {
  qRegisterMetaType<PasswordVerification>();
  qRegisterMetaType<AccountActionResult>();
  qRegisterMetaType<AccountProfile>();
  qRegisterMetaType<AccountSession>();
  qRegisterMetaType<AccountSessionPage>();
  qRegisterMetaType<TwoFactorSetup>();
  qRegisterMetaType<RecoveryCodes>();
  qRegisterMetaType<Passkey>();
  qRegisterMetaType<PasskeyPage>();
  qRegisterMetaType<PasskeyOptions>();
  qRegisterMetaType<PasskeyOperationResult>();
  qRegisterMetaType<PasskeyLoginResult>();

  connect(m_transport, &AdminTransport::allRequestsCanceled, this,
          [this]() { m_pending.clear(); });
  connect(m_transport, &AdminTransport::finished, this,
          [this](quint64 requestId, const TransportResponse &response) {
            handleResponse(requestId, response);
          });
}

void AccountClient::verifyPassword(const QString &password) {
  verifyPassword(QJsonObject{{QStringLiteral("password"), password}});
}

void AccountClient::verifyPassword(const QJsonObject &credentials) {
  const QString operation = QStringLiteral("auth.verify_password");
  if (credentials.isEmpty() ||
      credentials.value(QStringLiteral("password")).toString().isEmpty()) {
    sendError(invalidInput(operation, QStringLiteral("Password is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/auth/verify-password");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(credentials);
  sendRequest(PendingKind::VerifyPassword, request);
}

void AccountClient::updatePassword(const QString &currentPassword,
                                   const QString &newPassword,
                                   const QString &newPasswordConfirmation) {
  QJsonObject changes{
      {QStringLiteral("current_password"), currentPassword},
      {QStringLiteral("new_password"), newPassword},
  };
  if (!newPasswordConfirmation.isEmpty()) {
    changes.insert(QStringLiteral("new_password_confirmation"),
                   newPasswordConfirmation);
  }
  updatePassword(changes);
}

void AccountClient::updatePassword(const QJsonObject &changes) {
  const QString operation = QStringLiteral("auth.password.update");
  if (changes.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("Password update data is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PutOperation;
  request.path = QStringLiteral("/auth/password");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(changes);
  sendRequest(PendingKind::PasswordUpdate, request);
}

void AccountClient::updateProfile(const QJsonObject &changes) {
  const QString operation = QStringLiteral("profile.update");
  if (changes.isEmpty()) {
    sendError(invalidInput(operation,
                           QStringLiteral("Profile changes are required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PutOperation;
  request.path = QStringLiteral("/profile");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(changes);
  sendRequest(PendingKind::ProfileUpdate, request);
}

void AccountClient::listSessions(const QString &cursor, int limit) {
  const QString operation = QStringLiteral("profile.sessions");
  if (limit < 1 || limit > 100) {
    sendError(invalidInput(
        operation, QStringLiteral("Session page limit must be between 1 and "
                                  "100.")));
    return;
  }

  QUrlQuery query;
  query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
  if (!cursor.trimmed().isEmpty()) {
    query.addQueryItem(QStringLiteral("cursor"), cursor.trimmed());
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path =
      QStringLiteral("/profile/sessions?") + query.toString(QUrl::FullyEncoded);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::SessionsList, request);
}

void AccountClient::revokeSession(const QString &sessionId) {
  const QString operation = QStringLiteral("profile.session.revoke");
  const QString normalizedId = sessionId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Session ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::DeleteOperation;
  request.path = sessionPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::SessionRevoke, request, normalizedId);
}

void AccountClient::revokeOtherSessions() {
  const QString operation = QStringLiteral("profile.sessions.revoke_others");
  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/profile/sessions:revoke-others");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::SessionsRevokeOthers, request);
}

void AccountClient::setupTwoFactor(const QJsonObject &options) {
  const QString operation = QStringLiteral("auth.2fa.setup");
  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/auth/2fa/setup");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::TwoFactorSetup, request);
}

void AccountClient::enableTwoFactor(const QString &code,
                                    const QString &recoveryCode) {
  enableTwoFactor(codeBody(code, recoveryCode));
}

void AccountClient::enableTwoFactor(const QJsonObject &verification) {
  const QString operation = QStringLiteral("auth.2fa.enable");
  if (verification.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("Two-factor verification is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/auth/2fa/enable");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(verification);
  sendRequest(PendingKind::TwoFactorEnable, request);
}

void AccountClient::disableTwoFactor(const QString &code) {
  disableTwoFactor(codeBody(code));
}

void AccountClient::disableTwoFactor(const QJsonObject &verification) {
  const QString operation = QStringLiteral("auth.2fa.disable");
  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/auth/2fa/disable");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(verification);
  sendRequest(PendingKind::TwoFactorDisable, request);
}

void AccountClient::regenerateRecoveryCodes(const QString &code) {
  regenerateRecoveryCodes(codeBody(code));
}

void AccountClient::regenerateRecoveryCodes(const QJsonObject &verification) {
  const QString operation =
      QStringLiteral("auth.2fa.recovery_codes.regenerate");
  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/auth/2fa/recovery-codes:regenerate");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(verification);
  sendRequest(PendingKind::RecoveryCodesRegenerate, request);
}

void AccountClient::listPasskeys() {
  const QString operation = QStringLiteral("auth.passkeys.list");
  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = QStringLiteral("/auth/passkeys");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::PasskeysList, request);
}

void AccountClient::requestPasskeyRegistrationOptions(
    const QJsonObject &options) {
  const QString operation =
      QStringLiteral("auth.passkeys.registration.options");
  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/auth/passkeys/registration/options");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::PasskeyRegistrationOptions, request);
}

void AccountClient::verifyPasskeyRegistration(const QJsonObject &credential) {
  const QString operation = QStringLiteral("auth.passkeys.registration.verify");
  if (credential.isEmpty()) {
    sendError(invalidInput(operation,
                           QStringLiteral("Passkey credential is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/auth/passkeys/registration/verify");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(credential);
  sendRequest(PendingKind::PasskeyRegistrationVerify, request);
}

void AccountClient::requestPasskeyLoginOptions(const QJsonObject &options) {
  const QString operation = QStringLiteral("auth.passkeys.login.options");
  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/auth/passkeys/login/options");
  request.operation = operation;
  // The catalog marks both passkey login calls as public. The API key remains
  // present in AdminTransport, but no bearer access token is attached.
  request.authentication = RequestAuth::ApiKeyOnly;
  request.body = jsonBody(options);
  sendRequest(PendingKind::PasskeyLoginOptions, request);
}

void AccountClient::verifyPasskeyLogin(const QJsonObject &credential) {
  const QString operation = QStringLiteral("auth.passkeys.login.verify");
  if (credential.isEmpty()) {
    sendError(invalidInput(operation,
                           QStringLiteral("Passkey credential is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/auth/passkeys/login/verify");
  request.operation = operation;
  request.authentication = RequestAuth::ApiKeyOnly;
  request.body = jsonBody(credential);
  sendRequest(PendingKind::PasskeyLoginVerify, request);
}

void AccountClient::renamePasskey(const QString &passkeyId,
                                  const QString &name) {
  const QString operation = QStringLiteral("auth.passkey.rename");
  const QString normalizedId = passkeyId.trimmed();
  const QString normalizedName = name.trimmed();
  if (normalizedId.isEmpty() || normalizedName.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("Passkey ID and name are required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::CustomOperation;
  request.path = passkeyPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body =
      jsonBody(QJsonObject{{QStringLiteral("name"), normalizedName}});
  sendRequest(PendingKind::PasskeyRename, request, normalizedId);
}

void AccountClient::deletePasskey(const QString &passkeyId) {
  const QString operation = QStringLiteral("auth.passkey.delete");
  const QString normalizedId = passkeyId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Passkey ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::DeleteOperation;
  request.path = passkeyPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::PasskeyDelete, request, normalizedId);
}

void AccountClient::sendError(const AdminError &error) {
  emit errorOccurred(error);
}

void AccountClient::sendRequest(PendingKind kind,
                                const TransportRequest &request,
                                const QString &resourceId) {
  const quint64 requestId = m_transport->send(request);
  if (requestId == 0) {
    sendError(m_transport->configurationError(request.operation));
    return;
  }
  m_pending.insert(requestId, {kind, resourceId});
}

void AccountClient::handleResponse(quint64 requestId,
                                   const TransportResponse &response) {
  const auto pending = m_pending.find(requestId);
  if (pending == m_pending.end()) {
    return;
  }
  const PendingRequest pendingRequest = pending.value();
  m_pending.erase(pending);

  if (!response.isHttpSuccess()) {
    sendError(Internal::parseError(m_transport, response, response.operation));
    return;
  }

  QJsonObject object;
  if (!decodeResponse(response.body, &object)) {
    sendError(Internal::invalidJson(response.operation));
    return;
  }
  emit operationCompleted(response.operation, object);

  switch (pendingRequest.kind) {
  case PendingKind::VerifyPassword: {
    const PasswordVerification result = PasswordVerification::fromJson(object);
    if (!result.isValid()) {
      sendError(Internal::invalidModel(
          response.operation, QStringLiteral("password verification")));
      return;
    }
    emit passwordVerified(result);
    break;
  }
  case PendingKind::PasswordUpdate: {
    AccountActionResult result = AccountActionResult::fromJson(object);
    result.success = true;
    emit passwordUpdated(result);
    break;
  }
  case PendingKind::ProfileUpdate: {
    const AccountProfile profile = AccountProfile::fromJson(object);
    if (!profile.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("account profile")));
      return;
    }
    emit profileUpdated(profile);
    break;
  }
  case PendingKind::SessionsList: {
    const AccountSessionPage page = AccountSessionPage::fromJson(object);
    if (!page.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("account session page")));
      return;
    }
    emit sessionsReceived(page);
    break;
  }
  case PendingKind::SessionRevoke: {
    AccountActionResult result = AccountActionResult::fromJson(object);
    result.success = true;
    emit sessionRevoked(pendingRequest.resourceId, result);
    break;
  }
  case PendingKind::SessionsRevokeOthers: {
    AccountActionResult result = AccountActionResult::fromJson(object);
    result.success = true;
    emit otherSessionsRevoked(result);
    break;
  }
  case PendingKind::TwoFactorSetup: {
    const TwoFactorSetup setup = TwoFactorSetup::fromJson(object);
    if (!setup.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("two-factor setup")));
      return;
    }
    emit twoFactorSetupReceived(setup);
    break;
  }
  case PendingKind::TwoFactorEnable: {
    AccountActionResult result = AccountActionResult::fromJson(object);
    result.success = true;
    emit twoFactorEnabled(result);
    break;
  }
  case PendingKind::TwoFactorDisable: {
    AccountActionResult result = AccountActionResult::fromJson(object);
    result.success = true;
    emit twoFactorDisabled(result);
    break;
  }
  case PendingKind::RecoveryCodesRegenerate: {
    const RecoveryCodes codes = RecoveryCodes::fromJson(object);
    if (!codes.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("recovery codes")));
      return;
    }
    emit recoveryCodesRegenerated(codes);
    break;
  }
  case PendingKind::PasskeysList: {
    const PasskeyPage page = PasskeyPage::fromJson(object);
    if (!page.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("passkey page")));
      return;
    }
    emit passkeysReceived(page);
    break;
  }
  case PendingKind::PasskeyRegistrationOptions: {
    const PasskeyOptions options = PasskeyOptions::fromJson(object);
    if (!options.isValid()) {
      sendError(Internal::invalidModel(
          response.operation, QStringLiteral("passkey registration options")));
      return;
    }
    emit passkeyRegistrationOptionsReceived(options);
    break;
  }
  case PendingKind::PasskeyRegistrationVerify: {
    const PasskeyOperationResult result =
        PasskeyOperationResult::fromJson(object);
    if (!result.isValid()) {
      sendError(Internal::invalidModel(
          response.operation, QStringLiteral("passkey registration result")));
      return;
    }
    emit passkeyRegistered(result);
    break;
  }
  case PendingKind::PasskeyLoginOptions: {
    const PasskeyOptions options = PasskeyOptions::fromJson(object);
    if (!options.isValid()) {
      sendError(Internal::invalidModel(
          response.operation, QStringLiteral("passkey login options")));
      return;
    }
    emit passkeyLoginOptionsReceived(options);
    break;
  }
  case PendingKind::PasskeyLoginVerify: {
    const PasskeyLoginResult result = PasskeyLoginResult::fromJson(object);
    if (!result.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("passkey login result")));
      return;
    }
    emit passkeyLoginVerified(result);
    break;
  }
  case PendingKind::PasskeyRename: {
    PasskeyOperationResult result = PasskeyOperationResult::fromJson(object);
    if (!result.isValid()) {
      sendError(Internal::invalidModel(
          response.operation, QStringLiteral("passkey rename result")));
      return;
    }
    emit passkeyRenamed(pendingRequest.resourceId, result);
    break;
  }
  case PendingKind::PasskeyDelete: {
    AccountActionResult result = AccountActionResult::fromJson(object);
    result.success = true;
    emit passkeyDeleted(pendingRequest.resourceId, result);
    break;
  }
  }
}

} // namespace HubSight::Admin
