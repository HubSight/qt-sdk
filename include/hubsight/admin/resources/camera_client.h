#pragma once

#include "../admin_export.h"
#include "../admin_types.h"

#include <QHash>
#include <QObject>
#include <QString>

namespace HubSight::Admin {

class AdminTransport;
class AdminClient;

class HUBSIGHT_ADMIN_EXPORT CameraClient final : public QObject {
  Q_OBJECT

public:
  void list(const QString &cursor = {}, int limit = 50);
  void fetch(const QString &cameraId);

signals:
  void pageReceived(HubSight::Admin::CameraPage page);
  void cameraReceived(HubSight::Admin::CameraSummary camera);
  void errorOccurred(HubSight::Admin::AdminError error);

private:
  enum class PendingKind { List, Fetch };
  struct PendingRequest {
    PendingKind kind;
  };

  friend class AdminClient;
  CameraClient(AdminTransport *transport, QObject *parent = nullptr);

  AdminTransport *m_transport = nullptr;
  QHash<quint64, PendingRequest> m_pending;
};

} // namespace HubSight::Admin
