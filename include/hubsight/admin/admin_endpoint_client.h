#pragma once

#include "admin_endpoint_catalog.h"
#include "admin_export.h"
#include "admin_types.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>

namespace HubSight::Admin {

class AdminTransport;
class AdminClient;
struct TransportRequest;
struct TransportResponse;

// Generic fallback client for the complete Admin API v1 surface. Typed resource
// clients are preferred, but every catalog endpoint is also callable through
// this client while preserving the catalog method/path/auth contract.
//
// Request shape:
//   {"path_params": {"id": "..."}, "query": {"cursor": "..."},
//    "body": {...}, "timeout_ms": 15000, "idempotency_key": "..."}
// GET/DELETE use query/path_params; other HTTP methods use body. `body` may be
// a JSON object or array. Unknown control fields are not forwarded to the
// server.
class HUBSIGHT_ADMIN_EXPORT AdminEndpointClient final : public QObject {
  Q_OBJECT

public:
  // Invoke any catalog entry directly. This is useful for feature-gating and
  // keeps the endpoint registry usable before a typed resource client lands.
  void invoke(AdminEndpoint endpoint, const QJsonObject &request = {});

#define HUBSIGHT_ADMIN_ENDPOINT_DECLARATION(id, method, httpMethod, path,      \
                                            auth, permission, phase)           \
  void method(const QJsonObject &request = {});
  HUBSIGHT_ADMIN_ENDPOINTS(HUBSIGHT_ADMIN_ENDPOINT_DECLARATION)
#undef HUBSIGHT_ADMIN_ENDPOINT_DECLARATION

signals:
  void responseReceived(HubSight::Admin::AdminEndpoint endpoint,
                        QString operation, QJsonObject response,
                        QString requestId,
                        HubSight::Admin::HttpProtocol protocol);
  void operationCompleted(QString operation, QJsonObject response);
  void errorOccurred(HubSight::Admin::AdminError error);

private:
  enum class PendingKind { Generic };

  struct PendingRequest {
    AdminEndpoint endpoint = AdminEndpoint::SystemStatus;
    QString operation;
  };

  friend class AdminClient;
  explicit AdminEndpointClient(AdminTransport *transport,
                               QObject *parent = nullptr);

  void sendError(const AdminError &error);
  void sendRequest(AdminEndpoint endpoint, const TransportRequest &request);
  void handleResponse(quint64 requestId, const TransportResponse &response);

  AdminTransport *m_transport = nullptr;
  QHash<quint64, PendingRequest> m_pending;
};

} // namespace HubSight::Admin
