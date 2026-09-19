#include "../../include/hubsight/admin/resources/camera_client.h"

#include "../admin_transport.h"
#include "../resource_helpers.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QUrlQuery>

namespace HubSight::Admin {
namespace {

QString cameraPath(const QString &cameraId) {
  return QStringLiteral("/cameras/") +
         QString::fromUtf8(QUrl::toPercentEncoding(cameraId));
}

QJsonArray cameraArray(const QJsonObject &object) {
  const QJsonValue data = object.value(QStringLiteral("data"));
  if (data.isArray()) {
    return data.toArray();
  }
  const QJsonValue cameras = object.value(QStringLiteral("cameras"));
  return cameras.isArray() ? cameras.toArray() : QJsonArray{};
}

} // namespace

CameraClient::CameraClient(AdminTransport *transport, QObject *parent)
    : QObject(parent), m_transport(transport) {
  connect(m_transport, &AdminTransport::allRequestsCanceled, this,
          [this]() { m_pending.clear(); });
  connect(m_transport, &AdminTransport::finished, this,
          [this](quint64 requestId, const TransportResponse &response) {
            const auto pending = m_pending.find(requestId);
            if (pending == m_pending.end()) {
              return;
            }
            const PendingKind kind = pending->kind;
            m_pending.erase(pending);
            const QString operation = kind == PendingKind::List
                                          ? QStringLiteral("cameras.list")
                                          : QStringLiteral("cameras.get");

            if (!response.isHttpSuccess()) {
              emit errorOccurred(
                  Internal::parseError(m_transport, response, operation));
              return;
            }

            QJsonObject object;
            if (!Internal::decodeObject(response.body, &object)) {
              emit errorOccurred(Internal::invalidJson(operation));
              return;
            }

            if (kind == PendingKind::List) {
              CameraPage page;
              const QJsonArray values = cameraArray(object);
              page.items.reserve(values.size());
              for (const QJsonValue &value : values) {
                if (!value.isObject()) {
                  emit errorOccurred(Internal::invalidModel(
                      operation, QStringLiteral("camera list")));
                  return;
                }
                const CameraSummary camera =
                    CameraSummary::fromJson(value.toObject());
                if (!camera.isValid()) {
                  emit errorOccurred(Internal::invalidModel(
                      operation, QStringLiteral("camera list")));
                  return;
                }
                page.items.append(camera);
              }
              page.nextCursor =
                  object.value(QStringLiteral("next_cursor")).toString();
              page.hasMore = object.value(QStringLiteral("has_more"))
                                 .toBool(!page.nextCursor.isEmpty());
              emit pageReceived(page);
              return;
            }

            const QJsonValue data = object.value(QStringLiteral("data"));
            const QJsonObject cameraObject =
                data.isObject() ? data.toObject() : object;
            const CameraSummary camera = CameraSummary::fromJson(cameraObject);
            if (!camera.isValid()) {
              emit errorOccurred(
                  Internal::invalidModel(operation, QStringLiteral("camera")));
              return;
            }
            emit cameraReceived(camera);
          });
}

void CameraClient::list(const QString &cursor, int limit) {
  if (limit < 1 || limit > 100) {
    AdminError error;
    error.category = ErrorCategory::Validation;
    error.serverCode = QStringLiteral("INVALID_INPUT");
    error.developerMessage =
        QStringLiteral("Camera page limit must be between 1 and 100.");
    error.operation = QStringLiteral("cameras.list");
    emit errorOccurred(error);
    return;
  }

  QUrlQuery query;
  query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
  if (!cursor.trimmed().isEmpty()) {
    query.addQueryItem(QStringLiteral("cursor"), cursor.trimmed());
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path =
      QStringLiteral("/cameras?") + query.toString(QUrl::FullyEncoded);
  request.operation = QStringLiteral("cameras.list");
  request.authentication = RequestAuth::Protected;
  const quint64 requestId = m_transport->send(request);
  if (requestId == 0) {
    emit errorOccurred(m_transport->configurationError(request.operation));
    return;
  }
  m_pending.insert(requestId, {PendingKind::List});
}

void CameraClient::fetch(const QString &cameraId) {
  if (cameraId.trimmed().isEmpty()) {
    AdminError error;
    error.category = ErrorCategory::Validation;
    error.serverCode = QStringLiteral("INVALID_INPUT");
    error.developerMessage = QStringLiteral("Camera ID is required.");
    error.operation = QStringLiteral("cameras.get");
    emit errorOccurred(error);
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = cameraPath(cameraId.trimmed());
  request.operation = QStringLiteral("cameras.get");
  request.authentication = RequestAuth::Protected;
  const quint64 requestId = m_transport->send(request);
  if (requestId == 0) {
    emit errorOccurred(m_transport->configurationError(request.operation));
    return;
  }
  m_pending.insert(requestId, {PendingKind::Fetch});
}

} // namespace HubSight::Admin
