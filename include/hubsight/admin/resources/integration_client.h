#pragma once

#include "../admin_export.h"
#include "../admin_types.h"
#include "integration_types.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace HubSight::Admin {

class AdminTransport;
class AdminClient;
struct TransportRequest;
struct TransportResponse;

// Typed REST client for API clients, Google service accounts, app-configs,
// and long-running operation endpoints in the Admin API catalog.
class HUBSIGHT_ADMIN_EXPORT IntegrationClient final : public QObject {
  Q_OBJECT

public:
  void listClients(const QString &cursor = {}, int limit = 50,
                   const QJsonObject &filters = {});
  void createClient(const QJsonObject &client);
  void getClient(const QString &clientId);
  void patchClient(const QString &clientId, const QJsonObject &changes);
  void enableClient(const QString &clientId, const QJsonObject &options = {});
  void disableClient(const QString &clientId, const QJsonObject &options = {});
  void rotateClientKey(const QString &clientId,
                       const QJsonObject &options = {});
  void verifyClients(const QJsonObject &credentials = {});
  void deleteClient(const QString &clientId);

  void listGoogleServiceAccounts(const QString &cursor = {}, int limit = 50,
                                 const QJsonObject &filters = {});
  void importGoogleServiceAccount(const QJsonObject &account);
  void getGoogleServiceAccount(const QString &accountId);
  void activateGoogleServiceAccount(const QString &accountId,
                                    const QJsonObject &options = {});
  void testGoogleServiceAccount(const QString &accountId,
                                const QJsonObject &options = {});
  void fetchFirebasePreflight(const QString &accountId);
  void deleteGoogleServiceAccount(const QString &accountId);

  void listAppConfigs(const QString &cursor = {}, int limit = 50,
                      const QJsonObject &filters = {});
  void createAppConfig(const QJsonObject &config);
  void getAppConfig(const QString &configId);
  void requestAppConfigDownloadUrl(const QString &configId,
                                   const QJsonObject &options = {});
  void fetchAppConfigQr(const QString &configId);
  void revokeAppConfig(const QString &configId,
                       const QJsonObject &options = {});
  void deleteAppConfig(const QString &configId);

  void getOperation(const QString &operationId);
  void cancelOperation(const QString &operationId,
                       const QJsonObject &options = {});

signals:
  void clientsReceived(HubSight::Admin::ApiClientPage page);
  void clientCreated(HubSight::Admin::ApiClient client);
  void clientReceived(HubSight::Admin::ApiClient client);
  void clientPatched(QString clientId, HubSight::Admin::ApiClient client);
  void clientEnabled(QString clientId,
                     HubSight::Admin::IntegrationActionResult result);
  void clientDisabled(QString clientId,
                      HubSight::Admin::IntegrationActionResult result);
  void clientKeyRotated(QString clientId, HubSight::Admin::ApiClient client);
  void clientsVerified(HubSight::Admin::IntegrationActionResult result);
  void clientDeleted(QString clientId,
                     HubSight::Admin::IntegrationActionResult result);

  void
  googleServiceAccountsReceived(HubSight::Admin::GoogleServiceAccountPage page);
  void
  googleServiceAccountImported(HubSight::Admin::GoogleServiceAccount account);
  void
  googleServiceAccountReceived(HubSight::Admin::GoogleServiceAccount account);
  void googleServiceAccountActivated(
      QString accountId, HubSight::Admin::IntegrationActionResult result);
  void
  googleServiceAccountTested(QString accountId,
                             HubSight::Admin::IntegrationActionResult result);
  void
  firebasePreflightReceived(HubSight::Admin::FirebasePreflightResult result);
  void
  googleServiceAccountDeleted(QString accountId,
                              HubSight::Admin::IntegrationActionResult result);

  void appConfigsReceived(HubSight::Admin::AppConfigPage page);
  void appConfigCreated(HubSight::Admin::AppConfig config);
  void appConfigReceived(HubSight::Admin::AppConfig config);
  void
  appConfigDownloadUrlReceived(HubSight::Admin::IntegrationUrlResult result);
  void appConfigQrReceived(HubSight::Admin::IntegrationQrResult result);
  void appConfigRevoked(QString configId,
                        HubSight::Admin::IntegrationActionResult result);
  void appConfigDeleted(QString configId,
                        HubSight::Admin::IntegrationActionResult result);

  void operationReceived(HubSight::Admin::IntegrationOperation operation);
  void operationCanceled(QString operationId,
                         HubSight::Admin::IntegrationActionResult result);

  void operationCompleted(QString operation, QJsonObject response);
  void errorOccurred(HubSight::Admin::AdminError error);

private:
  enum class PendingKind {
    ClientsList,
    ClientCreate,
    ClientGet,
    ClientPatch,
    ClientEnable,
    ClientDisable,
    ClientRotateKey,
    ClientsVerify,
    ClientDelete,
    GoogleServiceAccountsList,
    GoogleServiceAccountImport,
    GoogleServiceAccountGet,
    GoogleServiceAccountActivate,
    GoogleServiceAccountTest,
    GoogleServiceAccountFirebasePreflight,
    GoogleServiceAccountDelete,
    AppConfigsList,
    AppConfigCreate,
    AppConfigGet,
    AppConfigDownloadUrl,
    AppConfigQr,
    AppConfigRevoke,
    AppConfigDelete,
    OperationGet,
    OperationCancel,
  };

  struct PendingRequest {
    PendingKind kind;
    QString resourceId;
  };

  friend class AdminClient;
  IntegrationClient(AdminTransport *transport, QObject *parent = nullptr);

  void sendError(const AdminError &error);
  void sendRequest(PendingKind kind, const TransportRequest &request,
                   const QString &resourceId = {});
  void handleResponse(quint64 requestId, const TransportResponse &response);

  AdminTransport *m_transport = nullptr;
  QHash<quint64, PendingRequest> m_pending;
};

} // namespace HubSight::Admin
