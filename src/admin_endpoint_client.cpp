#include "../include/hubsight/admin/admin_endpoint_client.h"

namespace HubSight::Admin {

AdminEndpointClient::AdminEndpointClient(AdminTransport *transport,
                                         QObject *parent)
    : QObject(parent), m_transport(transport) {}

void AdminEndpointClient::invoke(AdminEndpoint endpoint,
                                 const QJsonObject &request) {
  Q_UNUSED(request)
  const AdminEndpointDefinition &definition = adminEndpointDefinition(endpoint);

  AdminError error;
  error.category = ErrorCategory::Configuration;
  error.serverCode = QStringLiteral("SDK_ENDPOINT_NOT_IMPLEMENTED");
  error.developerMessage =
      QStringLiteral("This Admin API endpoint is reserved for a later SDK "
                     "phase.");
  error.operation = definition.operation;
  error.details = QJsonValue(QJsonObject{
      {QStringLiteral("method"), QString::fromLatin1(definition.method)},
      {QStringLiteral("path"), definition.pathTemplate},
      {QStringLiteral("requires_authentication"),
       definition.requiresAuthentication},
      {QStringLiteral("permission"), definition.permission},
      {QStringLiteral("phase"), definition.phase},
  });

  emit endpointNotImplemented(endpoint, definition.operation);
  emit errorOccurred(error);
}

#define HUBSIGHT_ADMIN_ENDPOINT_DEFINITION(id, method, httpMethod, path, auth, \
                                           permission, phase)                  \
  void AdminEndpointClient::method(const QJsonObject &request) {               \
    invoke(AdminEndpoint::id, request);                                        \
  }
HUBSIGHT_ADMIN_ENDPOINTS(HUBSIGHT_ADMIN_ENDPOINT_DEFINITION)
#undef HUBSIGHT_ADMIN_ENDPOINT_DEFINITION

} // namespace HubSight::Admin
