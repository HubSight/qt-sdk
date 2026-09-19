#include "../../include/hubsight/admin/resources/identity_client.h"

#include "../admin_transport.h"
#include "../resource_helpers.h"

#include <QJsonArray>
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

QString rolePath(const QString &roleId) {
  return QStringLiteral("/roles/") + encodedPathPart(roleId);
}

QString userPath(const QString &userId) {
  return QStringLiteral("/users/") + encodedPathPart(userId);
}

QString userActionPath(const QString &userId, const QString &action) {
  return userPath(userId) + QStringLiteral(":") + action;
}

QString userSessionsPath(const QString &userId) {
  return userPath(userId) + QStringLiteral("/sessions");
}

QString userSessionPath(const QString &userId, const QString &sessionId) {
  return userSessionsPath(userId) + QStringLiteral("/") +
         encodedPathPart(sessionId);
}

bool isReservedFilter(const QString &key) {
  return key == QStringLiteral("cursor") || key == QStringLiteral("limit");
}

QString queryValue(const QJsonValue &value) {
  if (value.isString()) {
    return value.toString();
  }
  if (value.isBool()) {
    return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  }
  if (value.isDouble()) {
    return QString::number(value.toDouble(), 'g', 15);
  }
  if (value.isArray()) {
    return QString::fromUtf8(
        QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
  }
  if (value.isObject()) {
    return QString::fromUtf8(
        QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
  }
  return {};
}

QString listPath(const QString &basePath, const QString &cursor, int limit,
                 const QJsonObject &filters) {
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
  if (!cursor.trimmed().isEmpty()) {
    query.addQueryItem(QStringLiteral("cursor"), cursor.trimmed());
  }
  for (auto it = filters.constBegin(); it != filters.constEnd(); ++it) {
    if (isReservedFilter(it.key()) || it.value().isNull() ||
        it.value().isUndefined()) {
      continue;
    }
    const QString value = queryValue(it.value());
    if (!value.isEmpty()) {
      query.addQueryItem(it.key(), value);
    }
  }
  return basePath + QStringLiteral("?") + query.toString(QUrl::FullyEncoded);
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

} // namespace

IdentityClient::IdentityClient(AdminTransport *transport, QObject *parent)
    : QObject(parent), m_transport(transport) {
  qRegisterMetaType<Permission>();
  qRegisterMetaType<PermissionPage>();
  qRegisterMetaType<Role>();
  qRegisterMetaType<RolePage>();
  qRegisterMetaType<User>();
  qRegisterMetaType<UserPage>();
  qRegisterMetaType<UserSession>();
  qRegisterMetaType<UserSessionPage>();
  qRegisterMetaType<IdentityActionResult>();

  connect(m_transport, &AdminTransport::allRequestsCanceled, this,
          [this]() { m_pending.clear(); });
  connect(m_transport, &AdminTransport::finished, this,
          [this](quint64 requestId, const TransportResponse &response) {
            handleResponse(requestId, response);
          });
}

void IdentityClient::listPermissions(const QString &cursor, int limit,
                                     const QJsonObject &filters) {
  const QString operation = QStringLiteral("permissions.list");
  if (limit < 1 || limit > 100) {
    sendError(invalidInput(
        operation, QStringLiteral("Permission page limit must be between 1 and "
                                  "100.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path =
      listPath(QStringLiteral("/permissions"), cursor, limit, filters);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::PermissionsList, request);
}

void IdentityClient::listRoles(const QString &cursor, int limit,
                               const QJsonObject &filters) {
  const QString operation = QStringLiteral("roles.list");
  if (limit < 1 || limit > 100) {
    sendError(invalidInput(
        operation,
        QStringLiteral("Role page limit must be between 1 and 100.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = listPath(QStringLiteral("/roles"), cursor, limit, filters);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::RolesList, request);
}

void IdentityClient::createRole(const QJsonObject &role) {
  const QString operation = QStringLiteral("roles.create");
  if (role.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Role data is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/roles");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(role);
  sendRequest(PendingKind::RoleCreate, request);
}

void IdentityClient::getRole(const QString &roleId) {
  const QString operation = QStringLiteral("roles.get");
  const QString normalizedId = roleId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(invalidInput(operation, QStringLiteral("Role ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = rolePath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::RoleGet, request, normalizedId);
}

void IdentityClient::patchRole(const QString &roleId,
                               const QJsonObject &changes) {
  const QString operation = QStringLiteral("roles.patch");
  const QString normalizedId = roleId.trimmed();
  if (normalizedId.isEmpty() || changes.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("Role ID and changes are required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::CustomOperation;
  request.path = rolePath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(changes);
  sendRequest(PendingKind::RolePatch, request, normalizedId);
}

void IdentityClient::deleteRole(const QString &roleId) {
  const QString operation = QStringLiteral("roles.delete");
  const QString normalizedId = roleId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(invalidInput(operation, QStringLiteral("Role ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::DeleteOperation;
  request.path = rolePath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::RoleDelete, request, normalizedId);
}

void IdentityClient::listUsers(const QString &cursor, int limit,
                               const QJsonObject &filters) {
  const QString operation = QStringLiteral("users.list");
  if (limit < 1 || limit > 100) {
    sendError(invalidInput(
        operation,
        QStringLiteral("User page limit must be between 1 and 100.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = listPath(QStringLiteral("/users"), cursor, limit, filters);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::UsersList, request);
}

void IdentityClient::createUser(const QJsonObject &user) {
  const QString operation = QStringLiteral("users.create");
  if (user.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("User data is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/users");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(user);
  sendRequest(PendingKind::UserCreate, request);
}

void IdentityClient::getUser(const QString &userId) {
  const QString operation = QStringLiteral("users.get");
  const QString normalizedId = userId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(invalidInput(operation, QStringLiteral("User ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = userPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::UserGet, request, normalizedId);
}

void IdentityClient::patchUser(const QString &userId,
                               const QJsonObject &changes) {
  const QString operation = QStringLiteral("users.patch");
  const QString normalizedId = userId.trimmed();
  if (normalizedId.isEmpty() || changes.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("User ID and changes are required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::CustomOperation;
  request.path = userPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(changes);
  sendRequest(PendingKind::UserPatch, request, normalizedId);
}

void IdentityClient::resetUserPassword(const QString &userId,
                                       const QJsonObject &options) {
  const QString operation = QStringLiteral("users.reset_password");
  const QString normalizedId = userId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(invalidInput(operation, QStringLiteral("User ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = userActionPath(normalizedId, QStringLiteral("reset-password"));
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::UserResetPassword, request, normalizedId);
}

void IdentityClient::blockUser(const QString &userId,
                               const QJsonObject &options) {
  const QString operation = QStringLiteral("users.block");
  const QString normalizedId = userId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(invalidInput(operation, QStringLiteral("User ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = userActionPath(normalizedId, QStringLiteral("block"));
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::UserBlock, request, normalizedId);
}

void IdentityClient::unblockUser(const QString &userId,
                                 const QJsonObject &options) {
  const QString operation = QStringLiteral("users.unblock");
  const QString normalizedId = userId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(invalidInput(operation, QStringLiteral("User ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = userActionPath(normalizedId, QStringLiteral("unblock"));
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::UserUnblock, request, normalizedId);
}

void IdentityClient::deleteUser(const QString &userId) {
  const QString operation = QStringLiteral("users.delete");
  const QString normalizedId = userId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(invalidInput(operation, QStringLiteral("User ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::DeleteOperation;
  request.path = userPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::UserDelete, request, normalizedId);
}

void IdentityClient::listUserSessions(const QString &userId,
                                      const QString &cursor, int limit,
                                      const QJsonObject &filters) {
  const QString operation = QStringLiteral("users.sessions.list");
  const QString normalizedId = userId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(invalidInput(operation, QStringLiteral("User ID is required.")));
    return;
  }
  if (limit < 1 || limit > 100) {
    sendError(invalidInput(
        operation, QStringLiteral("User session page limit must be between 1 "
                                  "and 100.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path =
      listPath(userSessionsPath(normalizedId), cursor, limit, filters);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::UserSessionsList, request, normalizedId);
}

void IdentityClient::deleteUserSession(const QString &userId,
                                       const QString &sessionId) {
  const QString operation = QStringLiteral("users.sessions.delete");
  const QString normalizedUserId = userId.trimmed();
  const QString normalizedSessionId = sessionId.trimmed();
  if (normalizedUserId.isEmpty() || normalizedSessionId.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("User ID and session ID are required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::DeleteOperation;
  request.path = userSessionPath(normalizedUserId, normalizedSessionId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::UserSessionDelete, request, normalizedUserId,
              normalizedSessionId);
}

void IdentityClient::revokeAllUserSessions(const QString &userId,
                                           const QJsonObject &options) {
  const QString operation = QStringLiteral("users.sessions.revoke_all");
  const QString normalizedId = userId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(invalidInput(operation, QStringLiteral("User ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = userActionPath(normalizedId, QStringLiteral("revoke-all"));
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::UserSessionsRevokeAll, request, normalizedId);
}

void IdentityClient::sendError(const AdminError &error) {
  emit errorOccurred(error);
}

void IdentityClient::sendRequest(PendingKind kind,
                                 const TransportRequest &request,
                                 const QString &resourceId,
                                 const QString &secondaryId) {
  const quint64 requestId = m_transport->send(request);
  if (requestId == 0) {
    sendError(m_transport->configurationError(request.operation));
    return;
  }
  m_pending.insert(requestId, {kind, resourceId, secondaryId});
}

void IdentityClient::handleResponse(quint64 requestId,
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

  const auto actionResult = [&]() {
    IdentityActionResult result = IdentityActionResult::fromJson(object);
    result.success = true;
    if (result.resourceId.isEmpty()) {
      result.resourceId = pendingRequest.resourceId;
    }
    return result;
  };

  switch (pendingRequest.kind) {
  case PendingKind::PermissionsList: {
    const PermissionPage page = PermissionPage::fromJson(object);
    if (!page.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("permission page")));
      return;
    }
    emit permissionsReceived(page);
    break;
  }
  case PendingKind::RolesList: {
    const RolePage page = RolePage::fromJson(object);
    if (!page.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("role page")));
      return;
    }
    emit rolesReceived(page);
    break;
  }
  case PendingKind::RoleCreate: {
    const Role role = Role::fromJson(object);
    if (!role.isValid()) {
      sendError(
          Internal::invalidModel(response.operation, QStringLiteral("role")));
      return;
    }
    emit roleCreated(role);
    break;
  }
  case PendingKind::RoleGet: {
    const Role role = Role::fromJson(object);
    if (!role.isValid()) {
      sendError(
          Internal::invalidModel(response.operation, QStringLiteral("role")));
      return;
    }
    emit roleReceived(role);
    break;
  }
  case PendingKind::RolePatch: {
    const Role role = Role::fromJson(object);
    if (!role.isValid()) {
      sendError(
          Internal::invalidModel(response.operation, QStringLiteral("role")));
      return;
    }
    emit rolePatched(pendingRequest.resourceId, role);
    break;
  }
  case PendingKind::RoleDelete:
    emit roleDeleted(pendingRequest.resourceId, actionResult());
    break;
  case PendingKind::UsersList: {
    const UserPage page = UserPage::fromJson(object);
    if (!page.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("user page")));
      return;
    }
    emit usersReceived(page);
    break;
  }
  case PendingKind::UserCreate: {
    const User user = User::fromJson(object);
    if (!user.isValid()) {
      sendError(
          Internal::invalidModel(response.operation, QStringLiteral("user")));
      return;
    }
    emit userCreated(user);
    break;
  }
  case PendingKind::UserGet: {
    const User user = User::fromJson(object);
    if (!user.isValid()) {
      sendError(
          Internal::invalidModel(response.operation, QStringLiteral("user")));
      return;
    }
    emit userReceived(user);
    break;
  }
  case PendingKind::UserPatch: {
    const User user = User::fromJson(object);
    if (!user.isValid()) {
      sendError(
          Internal::invalidModel(response.operation, QStringLiteral("user")));
      return;
    }
    emit userPatched(pendingRequest.resourceId, user);
    break;
  }
  case PendingKind::UserResetPassword:
    emit userPasswordReset(pendingRequest.resourceId, actionResult());
    break;
  case PendingKind::UserBlock:
    emit userBlocked(pendingRequest.resourceId, actionResult());
    break;
  case PendingKind::UserUnblock:
    emit userUnblocked(pendingRequest.resourceId, actionResult());
    break;
  case PendingKind::UserDelete:
    emit userDeleted(pendingRequest.resourceId, actionResult());
    break;
  case PendingKind::UserSessionsList: {
    const UserSessionPage page = UserSessionPage::fromJson(object);
    if (!page.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("user session page")));
      return;
    }
    emit userSessionsReceived(page);
    break;
  }
  case PendingKind::UserSessionDelete:
    emit userSessionDeleted(pendingRequest.resourceId,
                            pendingRequest.secondaryId, actionResult());
    break;
  case PendingKind::UserSessionsRevokeAll:
    emit allUserSessionsRevoked(pendingRequest.resourceId, actionResult());
    break;
  }
}

} // namespace HubSight::Admin
