#pragma once

#include "../admin_export.h"
#include "../admin_types.h"
#include "identity_types.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace HubSight::Admin {

class AdminTransport;
class AdminClient;
struct TransportRequest;
struct TransportResponse;

// Typed REST client for the deferred permissions, roles, users, and
// administrator-managed user-session endpoints in the Admin API catalog.
class HUBSIGHT_ADMIN_EXPORT IdentityClient final : public QObject {
  Q_OBJECT

public:
  void listPermissions(const QString &cursor = {}, int limit = 50,
                       const QJsonObject &filters = {});

  void listRoles(const QString &cursor = {}, int limit = 50,
                 const QJsonObject &filters = {});
  void createRole(const QJsonObject &role);
  void getRole(const QString &roleId);
  void patchRole(const QString &roleId, const QJsonObject &changes);
  void deleteRole(const QString &roleId);

  void listUsers(const QString &cursor = {}, int limit = 50,
                 const QJsonObject &filters = {});
  void createUser(const QJsonObject &user);
  void getUser(const QString &userId);
  void patchUser(const QString &userId, const QJsonObject &changes);
  void resetUserPassword(const QString &userId,
                         const QJsonObject &options = {});
  void blockUser(const QString &userId, const QJsonObject &options = {});
  void unblockUser(const QString &userId, const QJsonObject &options = {});
  void deleteUser(const QString &userId);

  void listUserSessions(const QString &userId, const QString &cursor = {},
                        int limit = 50, const QJsonObject &filters = {});
  void deleteUserSession(const QString &userId, const QString &sessionId);
  void revokeAllUserSessions(const QString &userId,
                             const QJsonObject &options = {});

signals:
  void permissionsReceived(HubSight::Admin::PermissionPage page);
  void rolesReceived(HubSight::Admin::RolePage page);
  void roleCreated(HubSight::Admin::Role role);
  void roleReceived(HubSight::Admin::Role role);
  void rolePatched(QString roleId, HubSight::Admin::Role role);
  void roleDeleted(QString roleId,
                   HubSight::Admin::IdentityActionResult result);

  void usersReceived(HubSight::Admin::UserPage page);
  void userCreated(HubSight::Admin::User user);
  void userReceived(HubSight::Admin::User user);
  void userPatched(QString userId, HubSight::Admin::User user);
  void userPasswordReset(QString userId,
                         HubSight::Admin::IdentityActionResult result);
  void userBlocked(QString userId,
                   HubSight::Admin::IdentityActionResult result);
  void userUnblocked(QString userId,
                     HubSight::Admin::IdentityActionResult result);
  void userDeleted(QString userId,
                   HubSight::Admin::IdentityActionResult result);

  void userSessionsReceived(HubSight::Admin::UserSessionPage page);
  void userSessionDeleted(QString userId, QString sessionId,
                          HubSight::Admin::IdentityActionResult result);
  void allUserSessionsRevoked(QString userId,
                              HubSight::Admin::IdentityActionResult result);

  void operationCompleted(QString operation, QJsonObject response);
  void errorOccurred(HubSight::Admin::AdminError error);

private:
  enum class PendingKind {
    PermissionsList,
    RolesList,
    RoleCreate,
    RoleGet,
    RolePatch,
    RoleDelete,
    UsersList,
    UserCreate,
    UserGet,
    UserPatch,
    UserResetPassword,
    UserBlock,
    UserUnblock,
    UserDelete,
    UserSessionsList,
    UserSessionDelete,
    UserSessionsRevokeAll,
  };

  struct PendingRequest {
    PendingKind kind;
    QString resourceId;
    QString secondaryId;
  };

  friend class AdminClient;
  IdentityClient(AdminTransport *transport, QObject *parent = nullptr);

  void sendError(const AdminError &error);
  void sendRequest(PendingKind kind, const TransportRequest &request,
                   const QString &resourceId = {},
                   const QString &secondaryId = {});
  void handleResponse(quint64 requestId, const TransportResponse &response);

  AdminTransport *m_transport = nullptr;
  QHash<quint64, PendingRequest> m_pending;
};

} // namespace HubSight::Admin
