#pragma once

#include "admin_endpoint_catalog.h"
#include "admin_export.h"
#include "admin_types.h"

#include <QJsonObject>
#include <QObject>

namespace HubSight::Admin {

class AdminTransport;
class AdminClient;

// Public facade for the complete Admin API v1 surface. Methods currently
// emit a synchronous not-implemented error; request contains the future
// path/query/body fields and is kept generic until each phase finalizes its
// typed DTOs and concurrency rules.
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
  void endpointNotImplemented(HubSight::Admin::AdminEndpoint endpoint,
                              QString operation);
  void errorOccurred(HubSight::Admin::AdminError error);

private:
  friend class AdminClient;
  explicit AdminEndpointClient(AdminTransport *transport,
                               QObject *parent = nullptr);

  AdminTransport *m_transport = nullptr;
};

} // namespace HubSight::Admin
