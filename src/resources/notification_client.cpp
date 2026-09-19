#include "../../include/hubsight/admin/resources/notification_client.h"

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

QString notificationPath(const QString &notificationId) {
  return QStringLiteral("/notifications/") + encodedPathPart(notificationId);
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
  if (value.isArray() || value.isObject()) {
    return QString::fromUtf8(
        QJsonDocument(value.isArray() ? QJsonDocument(value.toArray())
                                      : QJsonDocument(value.toObject()))
            .toJson(QJsonDocument::Compact));
  }
  return {};
}

bool decodeResponse(const QByteArray &body, QJsonObject *object) {
  if (body.trimmed().isEmpty()) {
    *object = {};
    return true;
  }
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
  if (document.isNull() || !document.isObject()) {
    return false;
  }
  *object = document.object();
  return true;
}

QJsonObject confirmationBody(const QString &confirmation) {
  return QJsonObject{{QStringLiteral("confirmation"), confirmation.trimmed()}};
}

} // namespace

NotificationClient::NotificationClient(AdminTransport *transport,
                                       QObject *parent)
    : QObject(parent), m_transport(transport) {
  qRegisterMetaType<Notification>();
  qRegisterMetaType<NotificationPage>();
  qRegisterMetaType<NotificationActionResult>();
  qRegisterMetaType<NotificationPushConfig>();

  connect(m_transport, &AdminTransport::allRequestsCanceled, this,
          [this]() { m_pending.clear(); });
  connect(m_transport, &AdminTransport::finished, this,
          [this](quint64 requestId, const TransportResponse &response) {
            handleResponse(requestId, response);
          });
}

void NotificationClient::list(const QString &cursor, int limit,
                              const QJsonObject &filters) {
  const QString operation = QStringLiteral("notifications.list");
  if (limit < 1 || limit > 100) {
    sendError(invalidInput(
        operation, QStringLiteral("Notification page limit must be between 1 "
                                  "and 100.")));
    return;
  }

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

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path =
      QStringLiteral("/notifications?") + query.toString(QUrl::FullyEncoded);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::List, request);
}

void NotificationClient::fetch(const QString &notificationId) {
  const QString operation = QStringLiteral("notifications.get");
  const QString normalizedId = notificationId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(invalidInput(operation,
                           QStringLiteral("Notification ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = notificationPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::Fetch, request, normalizedId);
}

void NotificationClient::patch(const QString &notificationId,
                               const QJsonObject &changes) {
  const QString operation = QStringLiteral("notifications.patch");
  const QString normalizedId = notificationId.trimmed();
  if (normalizedId.isEmpty() || changes.isEmpty()) {
    sendError(invalidInput(
        operation,
        QStringLiteral("Notification ID and changes are required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::CustomOperation;
  request.path = notificationPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(changes);
  sendRequest(PendingKind::Patch, request, normalizedId);
}

void NotificationClient::markAllRead(const QJsonObject &options) {
  const QString operation = QStringLiteral("notifications.read_all");
  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/notifications:read-all");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::MarkAllRead, request);
}

void NotificationClient::remove(const QString &notificationId,
                                const QString &confirmation) {
  const QString operation = QStringLiteral("notifications.delete");
  const QString normalizedId = notificationId.trimmed();
  const QString normalizedConfirmation = confirmation.trimmed();
  if (normalizedId.isEmpty() || normalizedConfirmation.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("Notification ID and confirmation are "
                                  "required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::DeleteOperation;
  request.path = notificationPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(confirmationBody(normalizedConfirmation));
  sendRequest(PendingKind::Delete, request, normalizedId);
}

void NotificationClient::removeBatch(const QStringList &notificationIds,
                                     const QString &confirmation) {
  const QString operation = QStringLiteral("notifications.batch_delete");
  QJsonArray ids;
  for (const QString &value : notificationIds) {
    const QString normalizedId = value.trimmed();
    if (!normalizedId.isEmpty() && !ids.contains(normalizedId)) {
      ids.append(normalizedId);
    }
  }
  const QString normalizedConfirmation = confirmation.trimmed();
  if (ids.isEmpty() || normalizedConfirmation.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("Notification IDs and confirmation are "
                                  "required.")));
    return;
  }

  QJsonObject body = confirmationBody(normalizedConfirmation);
  body.insert(QStringLiteral("notification_ids"), ids);
  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/notifications:batch-delete");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(body);
  sendRequest(PendingKind::BatchDelete, request);
}

void NotificationClient::clear(const QString &confirmation) {
  const QString operation = QStringLiteral("notifications.clear");
  const QString normalizedConfirmation = confirmation.trimmed();
  if (normalizedConfirmation.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Confirmation is required.")));
    return;
  }

  QJsonObject body = confirmationBody(normalizedConfirmation);
  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/notifications:clear");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(body);
  sendRequest(PendingKind::Clear, request);
}

void NotificationClient::sendTest(const QJsonObject &payload) {
  const QString operation = QStringLiteral("notifications.test");
  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/notifications:test");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(payload);
  sendRequest(PendingKind::Test, request);
}

void NotificationClient::fetchPushConfig() {
  const QString operation = QStringLiteral("notifications.push_config");
  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = QStringLiteral("/notifications/push-config");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::PushConfig, request);
}

void NotificationClient::upsertPushSubscription(
    const QJsonObject &subscription) {
  const QString operation =
      QStringLiteral("notifications.push_subscription_put");
  if (subscription.isEmpty()) {
    sendError(invalidInput(operation,
                           QStringLiteral("Push subscription is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PutOperation;
  request.path = QStringLiteral("/notifications/push-subscriptions/current");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(subscription);
  sendRequest(PendingKind::PushSubscriptionPut, request);
}

void NotificationClient::removePushSubscription() {
  const QString operation =
      QStringLiteral("notifications.push_subscription_delete");
  TransportRequest request;
  request.method = QNetworkAccessManager::DeleteOperation;
  request.path = QStringLiteral("/notifications/push-subscriptions/current");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::PushSubscriptionDelete, request);
}

void NotificationClient::sendError(const AdminError &error) {
  emit errorOccurred(error);
}

void NotificationClient::sendRequest(PendingKind kind,
                                     const TransportRequest &request,
                                     const QString &notificationId) {
  const quint64 requestId = m_transport->send(request);
  if (requestId == 0) {
    sendError(m_transport->configurationError(request.operation));
    return;
  }
  m_pending.insert(requestId, {kind, notificationId});
}

void NotificationClient::handleResponse(quint64 requestId,
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
  case PendingKind::List: {
    const NotificationPage page = NotificationPage::fromJson(object);
    if (!page.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("notification page")));
      return;
    }
    emit pageReceived(page);
    break;
  }
  case PendingKind::Fetch: {
    Notification notification = Notification::fromJson(object);
    if (notification.id.isEmpty()) {
      notification.id = pendingRequest.notificationId;
    }
    if (!notification.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("notification")));
      return;
    }
    emit notificationReceived(notification);
    break;
  }
  case PendingKind::Patch:
    emit notificationPatched(pendingRequest.notificationId,
                             NotificationActionResult::fromJson(object));
    break;
  case PendingKind::MarkAllRead:
    emit allNotificationsRead(NotificationActionResult::fromJson(object));
    break;
  case PendingKind::Delete:
    emit notificationDeleted(pendingRequest.notificationId,
                             NotificationActionResult::fromJson(object));
    break;
  case PendingKind::BatchDelete:
    emit notificationsBatchDeleted(NotificationActionResult::fromJson(object));
    break;
  case PendingKind::Clear:
    emit notificationsCleared(NotificationActionResult::fromJson(object));
    break;
  case PendingKind::Test:
    emit testSent(NotificationActionResult::fromJson(object));
    break;
  case PendingKind::PushConfig: {
    const NotificationPushConfig config =
        NotificationPushConfig::fromJson(object);
    if (!config.isValid()) {
      sendError(Internal::invalidModel(
          response.operation, QStringLiteral("notification push config")));
      return;
    }
    emit pushConfigReceived(config);
    break;
  }
  case PendingKind::PushSubscriptionPut:
    emit pushSubscriptionUpdated(NotificationActionResult::fromJson(object));
    break;
  case PendingKind::PushSubscriptionDelete:
    emit pushSubscriptionRemoved();
    break;
  }
}

} // namespace HubSight::Admin
