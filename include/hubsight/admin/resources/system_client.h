#pragma once

#include "../admin_export.h"
#include "../admin_types.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace HubSight::Admin {

class AdminTransport;
class AdminClient;

class HUBSIGHT_ADMIN_EXPORT SystemClient final : public QObject {
  Q_OBJECT

public:
  void fetchStatus();
  void fetchCapabilities();
  void fetchSettings();

signals:
  void statusReceived(HubSight::Admin::SystemStatus status);
  void capabilitiesReceived(HubSight::Admin::Capabilities capabilities);
  void settingsReceived(QJsonObject settings);
  void errorOccurred(HubSight::Admin::AdminError error);

private:
  enum class PendingKind { Status, Capabilities, Settings };
  struct PendingRequest {
    PendingKind kind;
  };

  friend class AdminClient;
  SystemClient(AdminTransport *transport, QObject *parent = nullptr);

  AdminTransport *m_transport = nullptr;
  QHash<quint64, PendingRequest> m_pending;
};

} // namespace HubSight::Admin
