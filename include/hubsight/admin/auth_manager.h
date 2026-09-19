#pragma once

#include "admin_export.h"
#include "admin_types.h"
#include "secure_storage.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <memory>

namespace HubSight::Admin {

class AdminTransport;
class AdminClient;
class SecureStorage;

class HUBSIGHT_ADMIN_EXPORT AuthManager final : public QObject {
  Q_OBJECT

public:
  bool isAuthenticated() const;
  AdminState state() const;
  AdminUser currentUser() const;

  // deviceInfo is passed as the Admin API's optional device_info object. The
  // SDK does not invent a browser fingerprint or send a device secret.
  void login(const QString &username, const QString &password,
             const QJsonObject &deviceInfo = {});
  void verifyTwoFactor(const QString &preAuthToken, const QString &code,
                       const QString &recoveryCode = {},
                       const QJsonObject &deviceInfo = {});
  void refresh();
  void restoreSession();
  void logout();
  void fetchCurrentUser();

signals:
  void stateChanged(HubSight::Admin::AdminState state);
  void loginSucceeded(HubSight::Admin::TokenSet tokens,
                      HubSight::Admin::AdminUser user);
  void twoFactorRequired(const QString &preAuthToken);
  void tokenRefreshed(HubSight::Admin::TokenSet tokens);
  void currentUserChanged(HubSight::Admin::AdminUser user);
  void loggedOut();
  void errorOccurred(HubSight::Admin::AdminError error);

private:
  enum class PendingKind {
    Login,
    VerifyTwoFactor,
    Refresh,
    Logout,
    CurrentUser,
  };

  struct PendingRequest {
    PendingKind kind;
    quint64 generation = 0;
  };

  friend class AdminClient;
  AuthManager(AdminTransport *transport, SecureStoragePtr storage,
              QObject *parent = nullptr);

  void setSecureStorage(SecureStoragePtr storage);
  void invalidateSession(bool clearPersistedToken = true);
  void handleResponse(PendingKind kind, const QByteArray &body, int statusCode,
                      const QString &requestIdHeader, int retryAfterSeconds,
                      int networkErrorCode, bool timedOut,
                      const QString &networkErrorText);
  void setState(AdminState state);
  void emitError(const AdminError &error);
  void clearLocalSession(AdminState nextState = AdminState::Unauthenticated,
                         bool clearPersistedToken = true);
  void completeTokenResponse(const QJsonObject &json, PendingKind kind,
                             const QString &requestId);

  AdminTransport *m_transport = nullptr;
  SecureStoragePtr m_storage;
  QString m_accessToken;
  AdminUser m_currentUser;
  AdminState m_state = AdminState::Unconfigured;
  QHash<quint64, PendingRequest> m_pending;
  quint64 m_generation = 0;
  bool m_refreshInFlight = false;
};

} // namespace HubSight::Admin
