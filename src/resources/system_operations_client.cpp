#include "../../include/hubsight/admin/resources/system_operations_client.h"

#include "../admin_transport.h"
#include "../resource_helpers.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
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

void addQuery(QUrlQuery *query, const QString &cursor, int limit,
              const QJsonObject &filters) {
  query->addQueryItem(QStringLiteral("limit"), QString::number(limit));
  if (!cursor.trimmed().isEmpty()) {
    query->addQueryItem(QStringLiteral("cursor"), cursor.trimmed());
  }
  for (auto it = filters.constBegin(); it != filters.constEnd(); ++it) {
    if (it.key() == QStringLiteral("cursor") ||
        it.key() == QStringLiteral("limit") || it.value().isNull() ||
        it.value().isUndefined()) {
      continue;
    }
    const QString value = queryValue(it.value());
    if (!value.isEmpty()) {
      query->addQueryItem(it.key(), value);
    }
  }
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

SystemOperationsClient::SystemOperationsClient(AdminTransport *transport,
                                               QObject *parent)
    : QObject(parent), m_transport(transport) {
  qRegisterMetaType<DashboardSummary>();
  qRegisterMetaType<DashboardActivity>();
  qRegisterMetaType<SystemHealth>();
  qRegisterMetaType<SystemSettingsPatchResult>();
  qRegisterMetaType<StorageCleanupResult>();
  qRegisterMetaType<AuditEventPage>();
  qRegisterMetaType<NvrStatus>();
  qRegisterMetaType<PoolStatus>();
  qRegisterMetaType<PoolSyncResult>();

  connect(m_transport, &AdminTransport::allRequestsCanceled, this,
          [this]() { m_pending.clear(); });
  connect(m_transport, &AdminTransport::finished, this,
          [this](quint64 requestId, const TransportResponse &response) {
            handleResponse(requestId, response);
          });
}

void SystemOperationsClient::fetchDashboardSummary() {
  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = QStringLiteral("/dashboard/summary");
  request.operation = QStringLiteral("dashboard.summary");
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::DashboardSummary, request);
}

void SystemOperationsClient::fetchDashboardActivity(
    const QString &cursor, int limit, const QJsonObject &filters) {
  const QString operation = QStringLiteral("dashboard.activity");
  if (limit < 1 || limit > 100) {
    sendError(invalidInput(
        operation,
        QStringLiteral("Dashboard activity page limit must be between "
                       "1 and 100.")));
    return;
  }

  QUrlQuery query;
  addQuery(&query, cursor, limit, filters);
  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = QStringLiteral("/dashboard/activity?") +
                 query.toString(QUrl::FullyEncoded);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::DashboardActivity, request);
}

void SystemOperationsClient::fetchHealth() {
  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = QStringLiteral("/system/health");
  request.operation = QStringLiteral("system.health");
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::Health, request);
}

void SystemOperationsClient::patchSettings(const QJsonObject &changes) {
  const QString operation = QStringLiteral("system.settings.patch");
  if (changes.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("System settings changes are required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::CustomOperation;
  request.path = QStringLiteral("/system/settings");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(changes);
  sendRequest(PendingKind::SettingsPatch, request);
}

void SystemOperationsClient::cleanupStorage(const QJsonObject &options) {
  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/system/storage:cleanup");
  request.operation = QStringLiteral("system.storage.cleanup");
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::StorageCleanup, request);
}

void SystemOperationsClient::listAuditEvents(const QString &cursor, int limit,
                                             const QJsonObject &filters) {
  const QString operation = QStringLiteral("system.audit_events");
  if (limit < 1 || limit > 100) {
    sendError(invalidInput(
        operation,
        QStringLiteral("Audit event page limit must be between 1 and "
                       "100.")));
    return;
  }

  QUrlQuery query;
  addQuery(&query, cursor, limit, filters);
  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = QStringLiteral("/system/audit-events?") +
                 query.toString(QUrl::FullyEncoded);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::AuditEvents, request);
}

void SystemOperationsClient::fetchNvrStatus() {
  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = QStringLiteral("/nvr/status");
  request.operation = QStringLiteral("nvr.status");
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::NvrStatus, request);
}

void SystemOperationsClient::fetchPoolStatus() {
  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = QStringLiteral("/pool/status");
  request.operation = QStringLiteral("pool.status");
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::PoolStatus, request);
}

void SystemOperationsClient::syncPool(const QJsonObject &options) {
  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/pool:sync");
  request.operation = QStringLiteral("pool.sync");
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::PoolSync, request);
}

void SystemOperationsClient::sendError(const AdminError &error) {
  emit errorOccurred(error);
}

void SystemOperationsClient::sendRequest(PendingKind kind,
                                         const TransportRequest &request) {
  const quint64 requestId = m_transport->send(request);
  if (requestId == 0) {
    sendError(m_transport->configurationError(request.operation));
    return;
  }
  m_pending.insert(requestId, {kind});
}

void SystemOperationsClient::handleResponse(quint64 requestId,
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
  case PendingKind::DashboardSummary: {
    const DashboardSummary summary = DashboardSummary::fromJson(object);
    if (!summary.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("dashboard summary")));
      return;
    }
    emit dashboardSummaryReceived(summary);
    break;
  }
  case PendingKind::DashboardActivity: {
    const DashboardActivity activity = DashboardActivity::fromJson(object);
    if (!activity.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("dashboard activity")));
      return;
    }
    emit dashboardActivityReceived(activity);
    break;
  }
  case PendingKind::Health: {
    const SystemHealth health = SystemHealth::fromJson(object);
    if (!health.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("system health")));
      return;
    }
    emit systemHealthReceived(health);
    break;
  }
  case PendingKind::SettingsPatch:
    emit settingsPatched(SystemSettingsPatchResult::fromJson(object));
    break;
  case PendingKind::StorageCleanup:
    emit storageCleanupCompleted(StorageCleanupResult::fromJson(object));
    break;
  case PendingKind::AuditEvents: {
    const AuditEventPage page = AuditEventPage::fromJson(object);
    if (!page.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("audit event page")));
      return;
    }
    emit auditEventsReceived(page);
    break;
  }
  case PendingKind::NvrStatus: {
    const NvrStatus status = NvrStatus::fromJson(object);
    if (!status.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("NVR status")));
      return;
    }
    emit nvrStatusReceived(status);
    break;
  }
  case PendingKind::PoolStatus: {
    const PoolStatus status = PoolStatus::fromJson(object);
    if (!status.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("pool status")));
      return;
    }
    emit poolStatusReceived(status);
    break;
  }
  case PendingKind::PoolSync:
    emit poolSyncCompleted(PoolSyncResult::fromJson(object));
    break;
  }
}

} // namespace HubSight::Admin
