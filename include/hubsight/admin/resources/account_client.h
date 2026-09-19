#pragma once

#include "../admin_export.h"
#include "../admin_types.h"
#include "account_types.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace HubSight::Admin {

class AdminTransport;
class AdminClient;
struct TransportRequest;
struct TransportResponse;

// Typed REST client for the deferred account, profile, session, 2FA, and
// passkey endpoints in the Admin API catalog. It does not change AuthManager's
// token or secure-storage lifecycle; passkey login results are reported only.
class HUBSIGHT_ADMIN_EXPORT AccountClient final : public QObject {
  Q_OBJECT

public:
  void verifyPassword(const QString &password);
  void verifyPassword(const QJsonObject &credentials);

  void updatePassword(const QString &currentPassword,
                      const QString &newPassword,
                      const QString &newPasswordConfirmation = {});
  void updatePassword(const QJsonObject &changes);
  void updateProfile(const QJsonObject &changes);

  void listSessions(const QString &cursor = {}, int limit = 50);
  void revokeSession(const QString &sessionId);
  void revokeOtherSessions();

  void setupTwoFactor(const QJsonObject &options = {});
  void enableTwoFactor(const QString &code, const QString &recoveryCode = {});
  void enableTwoFactor(const QJsonObject &verification);
  void disableTwoFactor(const QString &code = {});
  void disableTwoFactor(const QJsonObject &verification);
  void regenerateRecoveryCodes(const QString &code = {});
  void regenerateRecoveryCodes(const QJsonObject &verification);

  void listPasskeys();
  void requestPasskeyRegistrationOptions(const QJsonObject &options = {});
  void verifyPasskeyRegistration(const QJsonObject &credential);
  void requestPasskeyLoginOptions(const QJsonObject &options = {});
  void verifyPasskeyLogin(const QJsonObject &credential);
  void renamePasskey(const QString &passkeyId, const QString &name);
  void deletePasskey(const QString &passkeyId);

signals:
  void passwordVerified(HubSight::Admin::PasswordVerification result);
  void passwordUpdated(HubSight::Admin::AccountActionResult result);
  void profileUpdated(HubSight::Admin::AccountProfile profile);
  void sessionsReceived(HubSight::Admin::AccountSessionPage page);
  void sessionRevoked(QString sessionId,
                      HubSight::Admin::AccountActionResult result);
  void otherSessionsRevoked(HubSight::Admin::AccountActionResult result);
  void twoFactorSetupReceived(HubSight::Admin::TwoFactorSetup setup);
  void twoFactorEnabled(HubSight::Admin::AccountActionResult result);
  void twoFactorDisabled(HubSight::Admin::AccountActionResult result);
  void recoveryCodesRegenerated(HubSight::Admin::RecoveryCodes codes);
  void passkeysReceived(HubSight::Admin::PasskeyPage page);
  void
  passkeyRegistrationOptionsReceived(HubSight::Admin::PasskeyOptions options);
  void passkeyRegistered(HubSight::Admin::PasskeyOperationResult result);
  void passkeyLoginOptionsReceived(HubSight::Admin::PasskeyOptions options);
  void passkeyLoginVerified(HubSight::Admin::PasskeyLoginResult result);
  void passkeyRenamed(QString passkeyId,
                      HubSight::Admin::PasskeyOperationResult result);
  void passkeyDeleted(QString passkeyId,
                      HubSight::Admin::AccountActionResult result);
  void operationCompleted(QString operation, QJsonObject response);
  void errorOccurred(HubSight::Admin::AdminError error);

private:
  enum class PendingKind {
    VerifyPassword,
    PasswordUpdate,
    ProfileUpdate,
    SessionsList,
    SessionRevoke,
    SessionsRevokeOthers,
    TwoFactorSetup,
    TwoFactorEnable,
    TwoFactorDisable,
    RecoveryCodesRegenerate,
    PasskeysList,
    PasskeyRegistrationOptions,
    PasskeyRegistrationVerify,
    PasskeyLoginOptions,
    PasskeyLoginVerify,
    PasskeyRename,
    PasskeyDelete,
  };

  struct PendingRequest {
    PendingKind kind;
    QString resourceId;
  };

  friend class AdminClient;
  AccountClient(AdminTransport *transport, QObject *parent = nullptr);

  void sendError(const AdminError &error);
  void sendRequest(PendingKind kind, const TransportRequest &request,
                   const QString &resourceId = {});
  void handleResponse(quint64 requestId, const TransportResponse &response);

  AdminTransport *m_transport = nullptr;
  QHash<quint64, PendingRequest> m_pending;
};

} // namespace HubSight::Admin
