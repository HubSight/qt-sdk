#include "../../include/hubsight/admin/resources/system_client.h"

#include "../admin_transport.h"
#include "../resource_helpers.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>

namespace HubSight::Admin {

SystemClient::SystemClient(AdminTransport *transport, QObject *parent)
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

            const QString operation = [&]() {
              switch (kind) {
              case PendingKind::Status:
                return QStringLiteral("system.status");
              case PendingKind::Capabilities:
                return QStringLiteral("system.capabilities");
              case PendingKind::Settings:
                return QStringLiteral("system.settings");
              }
              return QStringLiteral("system");
            }();

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

            switch (kind) {
            case PendingKind::Status: {
              const SystemStatus status = SystemStatus::fromJson(object);
              if (status.apiVersion.isEmpty()) {
                emit errorOccurred(Internal::invalidModel(
                    operation, QStringLiteral("system status")));
                return;
              }
              emit statusReceived(status);
              break;
            }
            case PendingKind::Capabilities: {
              const Capabilities capabilities = Capabilities::fromJson(object);
              if (capabilities.matrixLimit <= 0) {
                emit errorOccurred(Internal::invalidModel(
                    operation, QStringLiteral("capabilities")));
                return;
              }
              emit capabilitiesReceived(capabilities);
              break;
            }
            case PendingKind::Settings: {
              const QJsonValue data = object.value(QStringLiteral("data"));
              emit settingsReceived(data.isObject() ? data.toObject() : object);
              break;
            }
            }
          });
}

void SystemClient::fetchStatus() {
  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = QStringLiteral("/system/status");
  request.operation = QStringLiteral("system.status");
  request.authentication = RequestAuth::ApiKeyOnly;
  const quint64 requestId = m_transport->send(request);
  if (requestId == 0) {
    emit errorOccurred(m_transport->configurationError(request.operation));
    return;
  }
  m_pending.insert(requestId, {PendingKind::Status});
}

void SystemClient::fetchCapabilities() {
  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = QStringLiteral("/system/capabilities");
  request.operation = QStringLiteral("system.capabilities");
  request.authentication = RequestAuth::Protected;
  const quint64 requestId = m_transport->send(request);
  if (requestId == 0) {
    emit errorOccurred(m_transport->configurationError(request.operation));
    return;
  }
  m_pending.insert(requestId, {PendingKind::Capabilities});
}

void SystemClient::fetchSettings() {
  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = QStringLiteral("/system/settings");
  request.operation = QStringLiteral("system.settings");
  request.authentication = RequestAuth::Protected;
  const quint64 requestId = m_transport->send(request);
  if (requestId == 0) {
    emit errorOccurred(m_transport->configurationError(request.operation));
    return;
  }
  m_pending.insert(requestId, {PendingKind::Settings});
}

} // namespace HubSight::Admin
