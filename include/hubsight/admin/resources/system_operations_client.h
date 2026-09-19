#pragma once

#include "../admin_export.h"
#include "../admin_types.h"
#include "system_operations_types.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace HubSight::Admin {

class AdminTransport;
class AdminClient;
struct TransportRequest;
struct TransportResponse;

// Typed REST client for dashboard, system-operation, NVR, and pool endpoints.
// Status/capability/settings reads remain on SystemClient.
class HUBSIGHT_ADMIN_EXPORT SystemOperationsClient final : public QObject {
  Q_OBJECT

public:
  void fetchDashboardSummary();
  void fetchDashboardActivity(const QString &cursor = {}, int limit = 50,
                              const QJsonObject &filters = {});
  void fetchHealth();
  void patchSettings(const QJsonObject &changes);
  void cleanupStorage(const QJsonObject &options = {});
  void listAuditEvents(const QString &cursor = {}, int limit = 50,
                       const QJsonObject &filters = {});
  void fetchNvrStatus();
  void fetchPoolStatus();
  void syncPool(const QJsonObject &options = {});

  // Explicit endpoint-oriented aliases for callers that prefer the catalog
  // names. They use the same requests and signals as the canonical methods.
  void fetchSystemHealth() { fetchHealth(); }
  void patchSystemSettings(const QJsonObject &changes) {
    patchSettings(changes);
  }
  void fetchAuditEvents(const QString &cursor = {}, int limit = 50,
                        const QJsonObject &filters = {}) {
    listAuditEvents(cursor, limit, filters);
  }
  void getDashboardSummary() { fetchDashboardSummary(); }
  void getDashboardActivity(const QString &cursor = {}, int limit = 50,
                            const QJsonObject &filters = {}) {
    fetchDashboardActivity(cursor, limit, filters);
  }
  void getNvrStatus() { fetchNvrStatus(); }
  void getPoolStatus() { fetchPoolStatus(); }

signals:
  void dashboardSummaryReceived(HubSight::Admin::DashboardSummary summary);
  void dashboardActivityReceived(HubSight::Admin::DashboardActivity activity);
  void systemHealthReceived(HubSight::Admin::SystemHealth health);
  void settingsPatched(HubSight::Admin::SystemSettingsPatchResult result);
  void storageCleanupCompleted(HubSight::Admin::StorageCleanupResult result);
  void auditEventsReceived(HubSight::Admin::AuditEventPage page);
  void nvrStatusReceived(HubSight::Admin::NvrStatus status);
  void poolStatusReceived(HubSight::Admin::PoolStatus status);
  void poolSyncCompleted(HubSight::Admin::PoolSyncResult result);
  void operationCompleted(QString operation, QJsonObject response);
  void errorOccurred(HubSight::Admin::AdminError error);

private:
  enum class PendingKind {
    DashboardSummary,
    DashboardActivity,
    Health,
    SettingsPatch,
    StorageCleanup,
    AuditEvents,
    NvrStatus,
    PoolStatus,
    PoolSync,
  };

  struct PendingRequest {
    PendingKind kind;
  };

  friend class AdminClient;
  SystemOperationsClient(AdminTransport *transport, QObject *parent = nullptr);

  void sendError(const AdminError &error);
  void sendRequest(PendingKind kind, const TransportRequest &request);
  void handleResponse(quint64 requestId, const TransportResponse &response);

  AdminTransport *m_transport = nullptr;
  QHash<quint64, PendingRequest> m_pending;
};

} // namespace HubSight::Admin
