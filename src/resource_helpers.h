#pragma once

#include "admin_transport.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace HubSight::Admin::Internal {

inline bool decodeObject(const QByteArray &body, QJsonObject *object) {
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(body, &error);
  if (document.isNull() || !document.isObject()) {
    return false;
  }
  *object = document.object();
  return true;
}

inline AdminError parseError(AdminTransport *transport,
                             const TransportResponse &response,
                             const QString &operation) {
  return transport->errorFor(response, operation);
}

inline AdminError invalidJson(const QString &operation) {
  AdminError error;
  error.category = ErrorCategory::Parse;
  error.serverCode = QStringLiteral("INVALID_JSON_RESPONSE");
  error.developerMessage = QStringLiteral("Admin API returned invalid JSON.");
  error.operation = operation;
  return error;
}

inline AdminError invalidModel(const QString &operation, const QString &model) {
  AdminError error;
  error.category = ErrorCategory::Parse;
  error.serverCode = QStringLiteral("INVALID_RESPONSE");
  error.developerMessage =
      QStringLiteral("Admin API returned an invalid %1 response.").arg(model);
  error.operation = operation;
  return error;
}

} // namespace HubSight::Admin::Internal
