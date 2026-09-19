#include "../include/hubsight/admin/admin_endpoint_catalog.h"

namespace HubSight::Admin {
namespace {

const QVector<AdminEndpointDefinition> kAdminEndpointCatalog = {
#define HUBSIGHT_ADMIN_ENDPOINT_DEFINITION(id, method, httpMethod, path, auth, \
                                           permission, phase)                  \
  {AdminEndpoint::id,                                                          \
   QByteArrayLiteral(httpMethod),                                              \
   QStringLiteral(path),                                                       \
   QStringLiteral(#method),                                                    \
   auth,                                                                       \
   QStringLiteral(permission),                                                 \
   QStringLiteral(phase)},
    HUBSIGHT_ADMIN_ENDPOINTS(HUBSIGHT_ADMIN_ENDPOINT_DEFINITION)
#undef HUBSIGHT_ADMIN_ENDPOINT_DEFINITION
};

const AdminEndpointDefinition kUnknownEndpoint{
    AdminEndpoint::SystemStatus,
    QByteArrayLiteral("UNKNOWN"),
    QStringLiteral(""),
    QStringLiteral("admin.unknown"),
    true,
    QStringLiteral(""),
    QStringLiteral("Unknown"),
};

} // namespace

QVector<AdminEndpointDefinition> adminEndpointCatalog() {
  return kAdminEndpointCatalog;
}

const AdminEndpointDefinition &adminEndpointDefinition(AdminEndpoint endpoint) {
  for (const AdminEndpointDefinition &definition : kAdminEndpointCatalog) {
    if (definition.endpoint == endpoint) {
      return definition;
    }
  }
  return kUnknownEndpoint;
}

} // namespace HubSight::Admin
