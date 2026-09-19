#include "../../include/hubsight/admin/resources/integration_client.h"

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

QString clientPath(const QString &clientId) {
  return QStringLiteral("/clients/") + encodedPathPart(clientId);
}

QString clientActionPath(const QString &clientId, const QString &action) {
  return clientPath(clientId) + QStringLiteral(":") + action;
}

QString googleServiceAccountPath(const QString &accountId) {
  return QStringLiteral("/google-service-accounts/") +
         encodedPathPart(accountId);
}

QString googleServiceAccountActionPath(const QString &accountId,
                                       const QString &action) {
  return googleServiceAccountPath(accountId) + QStringLiteral(":") + action;
}

QString appConfigPath(const QString &configId) {
  return QStringLiteral("/app-configs/") + encodedPathPart(configId);
}

QString appConfigActionPath(const QString &configId, const QString &action) {
  return appConfigPath(configId) + QStringLiteral(":") + action;
}

QString operationPath(const QString &operationId) {
  return QStringLiteral("/operations/") + encodedPathPart(operationId);
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

IntegrationClient::IntegrationClient(AdminTransport *transport, QObject *parent)
    : QObject(parent), m_transport(transport) {
  qRegisterMetaType<ApiClient>();
  qRegisterMetaType<ApiClientPage>();
  qRegisterMetaType<GoogleServiceAccount>();
  qRegisterMetaType<GoogleServiceAccountPage>();
  qRegisterMetaType<AppConfig>();
  qRegisterMetaType<AppConfigPage>();
  qRegisterMetaType<IntegrationActionResult>();
  qRegisterMetaType<IntegrationUrlResult>();
  qRegisterMetaType<IntegrationQrResult>();
  qRegisterMetaType<FirebasePreflightResult>();
  qRegisterMetaType<IntegrationOperation>();

  connect(m_transport, &AdminTransport::allRequestsCanceled, this,
          [this]() { m_pending.clear(); });
  connect(m_transport, &AdminTransport::finished, this,
          [this](quint64 requestId, const TransportResponse &response) {
            handleResponse(requestId, response);
          });
}

void IntegrationClient::listClients(const QString &cursor, int limit,
                                    const QJsonObject &filters) {
  const QString operation = QStringLiteral("clients.list");
  if (limit < 1 || limit > 100) {
    sendError(invalidInput(
        operation, QStringLiteral("API client page limit must be between 1 and "
                                  "100.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = listPath(QStringLiteral("/clients"), cursor, limit, filters);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::ClientsList, request);
}

void IntegrationClient::createClient(const QJsonObject &client) {
  const QString operation = QStringLiteral("clients.create");
  if (client.isEmpty()) {
    sendError(invalidInput(operation,
                           QStringLiteral("API client data is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/clients");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(client);
  sendRequest(PendingKind::ClientCreate, request);
}

void IntegrationClient::getClient(const QString &clientId) {
  const QString operation = QStringLiteral("clients.get");
  const QString normalizedId = clientId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("API client ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = clientPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::ClientGet, request, normalizedId);
}

void IntegrationClient::patchClient(const QString &clientId,
                                    const QJsonObject &changes) {
  const QString operation = QStringLiteral("clients.patch");
  const QString normalizedId = clientId.trimmed();
  if (normalizedId.isEmpty() || changes.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("API client ID and changes are required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::CustomOperation;
  request.path = clientPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(changes);
  sendRequest(PendingKind::ClientPatch, request, normalizedId);
}

void IntegrationClient::enableClient(const QString &clientId,
                                     const QJsonObject &options) {
  const QString operation = QStringLiteral("clients.enable");
  const QString normalizedId = clientId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("API client ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = clientActionPath(normalizedId, QStringLiteral("enable"));
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::ClientEnable, request, normalizedId);
}

void IntegrationClient::disableClient(const QString &clientId,
                                      const QJsonObject &options) {
  const QString operation = QStringLiteral("clients.disable");
  const QString normalizedId = clientId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("API client ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = clientActionPath(normalizedId, QStringLiteral("disable"));
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::ClientDisable, request, normalizedId);
}

void IntegrationClient::rotateClientKey(const QString &clientId,
                                        const QJsonObject &options) {
  const QString operation = QStringLiteral("clients.rotate_key");
  const QString normalizedId = clientId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("API client ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = clientActionPath(normalizedId, QStringLiteral("rotate-key"));
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::ClientRotateKey, request, normalizedId);
}

void IntegrationClient::verifyClients(const QJsonObject &credentials) {
  const QString operation = QStringLiteral("clients.verify");
  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/clients:verify");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(credentials);
  sendRequest(PendingKind::ClientsVerify, request);
}

void IntegrationClient::deleteClient(const QString &clientId) {
  const QString operation = QStringLiteral("clients.delete");
  const QString normalizedId = clientId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("API client ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::DeleteOperation;
  request.path = clientPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::ClientDelete, request, normalizedId);
}

void IntegrationClient::listGoogleServiceAccounts(const QString &cursor,
                                                  int limit,
                                                  const QJsonObject &filters) {
  const QString operation = QStringLiteral("google_service_accounts.list");
  if (limit < 1 || limit > 100) {
    sendError(invalidInput(
        operation, QStringLiteral("Service account page limit must be between "
                                  "1 and 100.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = listPath(QStringLiteral("/google-service-accounts"), cursor,
                          limit, filters);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::GoogleServiceAccountsList, request);
}

void IntegrationClient::importGoogleServiceAccount(const QJsonObject &account) {
  const QString operation = QStringLiteral("google_service_accounts.import");
  if (account.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("Google service account data is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/google-service-accounts:import");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(account);
  sendRequest(PendingKind::GoogleServiceAccountImport, request);
}

void IntegrationClient::getGoogleServiceAccount(const QString &accountId) {
  const QString operation = QStringLiteral("google_service_accounts.get");
  const QString normalizedId = accountId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(invalidInput(operation,
                           QStringLiteral("Service account ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = googleServiceAccountPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::GoogleServiceAccountGet, request, normalizedId);
}

void IntegrationClient::activateGoogleServiceAccount(
    const QString &accountId, const QJsonObject &options) {
  const QString operation = QStringLiteral("google_service_accounts.activate");
  const QString normalizedId = accountId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(invalidInput(operation,
                           QStringLiteral("Service account ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path =
      googleServiceAccountActionPath(normalizedId, QStringLiteral("activate"));
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::GoogleServiceAccountActivate, request, normalizedId);
}

void IntegrationClient::testGoogleServiceAccount(const QString &accountId,
                                                 const QJsonObject &options) {
  const QString operation = QStringLiteral("google_service_accounts.test");
  const QString normalizedId = accountId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(invalidInput(operation,
                           QStringLiteral("Service account ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path =
      googleServiceAccountActionPath(normalizedId, QStringLiteral("test"));
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::GoogleServiceAccountTest, request, normalizedId);
}

void IntegrationClient::fetchFirebasePreflight(const QString &accountId) {
  const QString operation =
      QStringLiteral("google_service_accounts.firebase_preflight");
  const QString normalizedId = accountId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(invalidInput(operation,
                           QStringLiteral("Service account ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = googleServiceAccountPath(normalizedId) +
                 QStringLiteral("/firebase-preflight");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::GoogleServiceAccountFirebasePreflight, request,
              normalizedId);
}

void IntegrationClient::deleteGoogleServiceAccount(const QString &accountId) {
  const QString operation = QStringLiteral("google_service_accounts.delete");
  const QString normalizedId = accountId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(invalidInput(operation,
                           QStringLiteral("Service account ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::DeleteOperation;
  request.path = googleServiceAccountPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::GoogleServiceAccountDelete, request, normalizedId);
}

void IntegrationClient::listAppConfigs(const QString &cursor, int limit,
                                       const QJsonObject &filters) {
  const QString operation = QStringLiteral("app_configs.list");
  if (limit < 1 || limit > 100) {
    sendError(invalidInput(
        operation, QStringLiteral("App config page limit must be between 1 and "
                                  "100.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path =
      listPath(QStringLiteral("/app-configs"), cursor, limit, filters);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::AppConfigsList, request);
}

void IntegrationClient::createAppConfig(const QJsonObject &config) {
  const QString operation = QStringLiteral("app_configs.create");
  if (config.isEmpty()) {
    sendError(invalidInput(operation,
                           QStringLiteral("App config data is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/app-configs");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(config);
  sendRequest(PendingKind::AppConfigCreate, request);
}

void IntegrationClient::getAppConfig(const QString &configId) {
  const QString operation = QStringLiteral("app_configs.get");
  const QString normalizedId = configId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("App config ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = appConfigPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::AppConfigGet, request, normalizedId);
}

void IntegrationClient::requestAppConfigDownloadUrl(
    const QString &configId, const QJsonObject &options) {
  const QString operation = QStringLiteral("app_configs.download_url");
  const QString normalizedId = configId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("App config ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = appConfigPath(normalizedId) + QStringLiteral("/download-url");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::AppConfigDownloadUrl, request, normalizedId);
}

void IntegrationClient::fetchAppConfigQr(const QString &configId) {
  const QString operation = QStringLiteral("app_configs.qr");
  const QString normalizedId = configId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("App config ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = appConfigPath(normalizedId) + QStringLiteral("/qr");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::AppConfigQr, request, normalizedId);
}

void IntegrationClient::revokeAppConfig(const QString &configId,
                                        const QJsonObject &options) {
  const QString operation = QStringLiteral("app_configs.revoke");
  const QString normalizedId = configId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("App config ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = appConfigActionPath(normalizedId, QStringLiteral("revoke"));
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::AppConfigRevoke, request, normalizedId);
}

void IntegrationClient::deleteAppConfig(const QString &configId) {
  const QString operation = QStringLiteral("app_configs.delete");
  const QString normalizedId = configId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("App config ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::DeleteOperation;
  request.path = appConfigPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::AppConfigDelete, request, normalizedId);
}

void IntegrationClient::getOperation(const QString &operationId) {
  const QString operation = QStringLiteral("operations.get");
  const QString normalizedId = operationId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Operation ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = operationPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::OperationGet, request, normalizedId);
}

void IntegrationClient::cancelOperation(const QString &operationId,
                                        const QJsonObject &options) {
  const QString operation = QStringLiteral("operations.cancel");
  const QString normalizedId = operationId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Operation ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = operationPath(normalizedId) + QStringLiteral(":cancel");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::OperationCancel, request, normalizedId);
}

void IntegrationClient::sendError(const AdminError &error) {
  emit errorOccurred(error);
}

void IntegrationClient::sendRequest(PendingKind kind,
                                    const TransportRequest &request,
                                    const QString &resourceId) {
  const quint64 requestId = m_transport->send(request);
  if (requestId == 0) {
    sendError(m_transport->configurationError(request.operation));
    return;
  }
  m_pending.insert(requestId, {kind, resourceId});
}

void IntegrationClient::handleResponse(quint64 requestId,
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
    IntegrationActionResult result = IntegrationActionResult::fromJson(object);
    result.success = true;
    if (result.resourceId.isEmpty()) {
      result.resourceId = pendingRequest.resourceId;
    }
    return result;
  };

  switch (pendingRequest.kind) {
  case PendingKind::ClientsList: {
    const ApiClientPage page = ApiClientPage::fromJson(object);
    if (!page.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("API client page")));
      return;
    }
    emit clientsReceived(page);
    break;
  }
  case PendingKind::ClientCreate: {
    const ApiClient client = ApiClient::fromJson(object);
    if (!client.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("API client")));
      return;
    }
    emit clientCreated(client);
    break;
  }
  case PendingKind::ClientGet: {
    const ApiClient client = ApiClient::fromJson(object);
    if (!client.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("API client")));
      return;
    }
    emit clientReceived(client);
    break;
  }
  case PendingKind::ClientPatch: {
    const ApiClient client = ApiClient::fromJson(object);
    if (!client.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("API client")));
      return;
    }
    emit clientPatched(pendingRequest.resourceId, client);
    break;
  }
  case PendingKind::ClientEnable:
    emit clientEnabled(pendingRequest.resourceId, actionResult());
    break;
  case PendingKind::ClientDisable:
    emit clientDisabled(pendingRequest.resourceId, actionResult());
    break;
  case PendingKind::ClientRotateKey: {
    const ApiClient client = ApiClient::fromJson(object);
    if (!client.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("rotated API client")));
      return;
    }
    emit clientKeyRotated(pendingRequest.resourceId, client);
    break;
  }
  case PendingKind::ClientsVerify:
    emit clientsVerified(actionResult());
    break;
  case PendingKind::ClientDelete:
    emit clientDeleted(pendingRequest.resourceId, actionResult());
    break;
  case PendingKind::GoogleServiceAccountsList: {
    const GoogleServiceAccountPage page =
        GoogleServiceAccountPage::fromJson(object);
    if (!page.isValid()) {
      sendError(Internal::invalidModel(
          response.operation, QStringLiteral("Google service account page")));
      return;
    }
    emit googleServiceAccountsReceived(page);
    break;
  }
  case PendingKind::GoogleServiceAccountImport: {
    const GoogleServiceAccount account = GoogleServiceAccount::fromJson(object);
    if (!account.isValid()) {
      sendError(Internal::invalidModel(
          response.operation, QStringLiteral("Google service account")));
      return;
    }
    emit googleServiceAccountImported(account);
    break;
  }
  case PendingKind::GoogleServiceAccountGet: {
    const GoogleServiceAccount account = GoogleServiceAccount::fromJson(object);
    if (!account.isValid()) {
      sendError(Internal::invalidModel(
          response.operation, QStringLiteral("Google service account")));
      return;
    }
    emit googleServiceAccountReceived(account);
    break;
  }
  case PendingKind::GoogleServiceAccountActivate:
    emit googleServiceAccountActivated(pendingRequest.resourceId,
                                       actionResult());
    break;
  case PendingKind::GoogleServiceAccountTest:
    emit googleServiceAccountTested(pendingRequest.resourceId, actionResult());
    break;
  case PendingKind::GoogleServiceAccountFirebasePreflight: {
    const FirebasePreflightResult result =
        FirebasePreflightResult::fromJson(object, pendingRequest.resourceId);
    if (!result.isValid()) {
      sendError(Internal::invalidModel(
          response.operation, QStringLiteral("Firebase preflight result")));
      return;
    }
    emit firebasePreflightReceived(result);
    break;
  }
  case PendingKind::GoogleServiceAccountDelete:
    emit googleServiceAccountDeleted(pendingRequest.resourceId, actionResult());
    break;
  case PendingKind::AppConfigsList: {
    const AppConfigPage page = AppConfigPage::fromJson(object);
    if (!page.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("app config page")));
      return;
    }
    emit appConfigsReceived(page);
    break;
  }
  case PendingKind::AppConfigCreate: {
    const AppConfig config = AppConfig::fromJson(object);
    if (!config.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("app config")));
      return;
    }
    emit appConfigCreated(config);
    break;
  }
  case PendingKind::AppConfigGet: {
    const AppConfig config = AppConfig::fromJson(object);
    if (!config.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("app config")));
      return;
    }
    emit appConfigReceived(config);
    break;
  }
  case PendingKind::AppConfigDownloadUrl: {
    const IntegrationUrlResult result =
        IntegrationUrlResult::fromJson(object, pendingRequest.resourceId);
    if (!result.isValid()) {
      sendError(Internal::invalidModel(
          response.operation, QStringLiteral("app config download URL")));
      return;
    }
    emit appConfigDownloadUrlReceived(result);
    break;
  }
  case PendingKind::AppConfigQr: {
    const IntegrationQrResult result =
        IntegrationQrResult::fromJson(object, pendingRequest.resourceId);
    if (!result.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("app config QR")));
      return;
    }
    emit appConfigQrReceived(result);
    break;
  }
  case PendingKind::AppConfigRevoke:
    emit appConfigRevoked(pendingRequest.resourceId, actionResult());
    break;
  case PendingKind::AppConfigDelete:
    emit appConfigDeleted(pendingRequest.resourceId, actionResult());
    break;
  case PendingKind::OperationGet: {
    const IntegrationOperation operation =
        IntegrationOperation::fromJson(object);
    if (!operation.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("operation")));
      return;
    }
    emit operationReceived(operation);
    break;
  }
  case PendingKind::OperationCancel:
    emit operationCanceled(pendingRequest.resourceId, actionResult());
    break;
  }
}

} // namespace HubSight::Admin
