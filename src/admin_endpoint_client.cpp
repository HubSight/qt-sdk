#include "../include/hubsight/admin/admin_endpoint_client.h"

#include "admin_transport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>

namespace HubSight::Admin {
namespace {

QString queryString(const QJsonValue &value) {
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

QNetworkAccessManager::Operation operationFor(const QByteArray &method) {
  if (method == QByteArrayLiteral("GET")) {
    return QNetworkAccessManager::GetOperation;
  }
  if (method == QByteArrayLiteral("POST")) {
    return QNetworkAccessManager::PostOperation;
  }
  if (method == QByteArrayLiteral("PUT")) {
    return QNetworkAccessManager::PutOperation;
  }
  if (method == QByteArrayLiteral("DELETE")) {
    return QNetworkAccessManager::DeleteOperation;
  }
  return QNetworkAccessManager::CustomOperation;
}

AdminError localError(ErrorCategory category, const QString &code,
                      const QString &message, const QString &operation) {
  AdminError error;
  error.category = category;
  error.serverCode = code;
  error.developerMessage = message;
  error.operation = operation;
  return error;
}

} // namespace

AdminEndpointClient::AdminEndpointClient(AdminTransport *transport,
                                         QObject *parent)
    : QObject(parent), m_transport(transport) {
  qRegisterMetaType<AdminEndpoint>();
  connect(m_transport, &AdminTransport::allRequestsCanceled, this,
          [this]() { m_pending.clear(); });
  connect(m_transport, &AdminTransport::finished, this,
          [this](quint64 requestId, const TransportResponse &response) {
            handleResponse(requestId, response);
          });
}

void AdminEndpointClient::invoke(AdminEndpoint endpoint,
                                 const QJsonObject &request) {
  const AdminEndpointDefinition &definition = adminEndpointDefinition(endpoint);
  const QString operation = definition.operation;

  if (definition.method == QByteArrayLiteral("WEBSOCKET")) {
    sendError(localError(
        ErrorCategory::Validation, QStringLiteral("USE_REALTIME_CLIENT"),
        QStringLiteral(
            "The Admin realtime WebSocket endpoint is managed by the typed "
            "relay client, not the generic HTTP endpoint client."),
        operation));
    return;
  }

  QString path = definition.pathTemplate;
  const QJsonObject pathParams =
      request.value(QStringLiteral("path_params")).toObject();
  for (auto it = pathParams.constBegin(); it != pathParams.constEnd(); ++it) {
    const QString placeholder =
        QStringLiteral("{") + it.key() + QStringLiteral("}");
    const QString value = queryString(it.value());
    if (value.isEmpty()) {
      continue;
    }
    path.replace(placeholder,
                 QString::fromUtf8(QUrl::toPercentEncoding(value)));
  }
  if (path.contains(QLatin1Char('{')) || path.contains(QLatin1Char('}'))) {
    sendError(localError(
        ErrorCategory::Validation, QStringLiteral("MISSING_PATH_PARAMETER"),
        QStringLiteral("The generic Admin endpoint request is missing a path "
                       "parameter."),
        operation));
    return;
  }

  const QJsonObject query = request.value(QStringLiteral("query")).toObject();
  if (!query.isEmpty()) {
    QUrlQuery urlQuery;
    for (auto it = query.constBegin(); it != query.constEnd(); ++it) {
      if (it.value().isNull() || it.value().isUndefined()) {
        continue;
      }
      const QString value = queryString(it.value());
      if (!value.isEmpty()) {
        urlQuery.addQueryItem(it.key(), value);
      }
    }
    if (!urlQuery.isEmpty()) {
      path += QStringLiteral("?") + urlQuery.toString(QUrl::FullyEncoded);
    }
  }

  const QNetworkAccessManager::Operation method =
      operationFor(definition.method);
  if (method == QNetworkAccessManager::CustomOperation &&
      definition.method != QByteArrayLiteral("PATCH")) {
    sendError(localError(
        ErrorCategory::Validation, QStringLiteral("UNSUPPORTED_HTTP_METHOD"),
        QStringLiteral("The generic Admin endpoint client does not support "
                       "this catalog method."),
        operation));
    return;
  }

  QByteArray serializedBody;
  const QJsonValue explicitBody = request.value(QStringLiteral("body"));
  if (explicitBody.isObject()) {
    serializedBody =
        QJsonDocument(explicitBody.toObject()).toJson(QJsonDocument::Compact);
  } else if (explicitBody.isArray()) {
    serializedBody =
        QJsonDocument(explicitBody.toArray()).toJson(QJsonDocument::Compact);
  } else if (!explicitBody.isUndefined() && !explicitBody.isNull()) {
    sendError(localError(
        ErrorCategory::Validation, QStringLiteral("INVALID_JSON_BODY"),
        QStringLiteral("The generic Admin endpoint body must be a JSON object "
                       "or array."),
        operation));
    return;
  } else if (method != QNetworkAccessManager::GetOperation &&
             method != QNetworkAccessManager::DeleteOperation) {
    static const QSet<QString> controlKeys{
        QStringLiteral("path_params"), QStringLiteral("query"),
        QStringLiteral("body"), QStringLiteral("timeout_ms"),
        QStringLiteral("idempotency_key")};
    QJsonObject inferredBody;
    for (auto it = request.constBegin(); it != request.constEnd(); ++it) {
      if (!controlKeys.contains(it.key())) {
        inferredBody.insert(it.key(), it.value());
      }
    }
    if (!inferredBody.isEmpty()) {
      serializedBody =
          QJsonDocument(inferredBody).toJson(QJsonDocument::Compact);
    }
  }

  TransportRequest transportRequest;
  transportRequest.method = method;
  transportRequest.path = path;
  transportRequest.operation = operation;
  transportRequest.authentication = definition.requiresAuthentication
                                        ? RequestAuth::Protected
                                        : RequestAuth::ApiKeyOnly;
  transportRequest.body = serializedBody;
  transportRequest.timeoutMs = qBound(
      1000, request.value(QStringLiteral("timeout_ms")).toInt(15000), 120000);
  transportRequest.idempotencyKey =
      request.value(QStringLiteral("idempotency_key")).toString();
  sendRequest(endpoint, transportRequest);
}

void AdminEndpointClient::sendError(const AdminError &error) {
  emit errorOccurred(error);
}

void AdminEndpointClient::sendRequest(AdminEndpoint endpoint,
                                      const TransportRequest &request) {
  const quint64 requestId = m_transport->send(request);
  if (requestId == 0) {
    sendError(m_transport->configurationError(request.operation));
    return;
  }
  m_pending.insert(requestId, {endpoint, request.operation});
}

void AdminEndpointClient::handleResponse(quint64 requestId,
                                         const TransportResponse &response) {
  const auto pending = m_pending.find(requestId);
  if (pending == m_pending.end()) {
    return;
  }
  const PendingRequest pendingRequest = pending.value();
  m_pending.erase(pending);

  if (!response.isHttpSuccess()) {
    sendError(m_transport->errorFor(response, response.operation));
    return;
  }

  QJsonObject object;
  if (!response.body.trimmed().isEmpty()) {
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(response.body, &parseError);
    if (document.isNull()) {
      sendError(localError(
          ErrorCategory::Parse, QStringLiteral("INVALID_JSON_RESPONSE"),
          QStringLiteral("Admin endpoint returned invalid JSON."),
          response.operation));
      return;
    }
    if (document.isObject()) {
      object = document.object();
    } else if (document.isArray()) {
      object.insert(QStringLiteral("items"), document.array());
    } else {
      sendError(localError(
          ErrorCategory::Parse, QStringLiteral("INVALID_JSON_RESPONSE"),
          QStringLiteral("Admin endpoint returned a JSON scalar response."),
          response.operation));
      return;
    }
  }

  emit operationCompleted(response.operation, object);
  emit responseReceived(pendingRequest.endpoint, response.operation, object,
                        response.requestId, response.protocol);
}

#define HUBSIGHT_ADMIN_ENDPOINT_DEFINITION(id, method, httpMethod, path, auth, \
                                           permission, phase)                  \
  void AdminEndpointClient::method(const QJsonObject &request) {               \
    invoke(AdminEndpoint::id, request);                                        \
  }
HUBSIGHT_ADMIN_ENDPOINTS(HUBSIGHT_ADMIN_ENDPOINT_DEFINITION)
#undef HUBSIGHT_ADMIN_ENDPOINT_DEFINITION

} // namespace HubSight::Admin
