#pragma once

#include "../admin_export.h"
#include "../admin_types.h"
#include "camera_management_types.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace HubSight::Admin {

class AdminTransport;
class AdminClient;
struct TransportRequest;
struct TransportResponse;

// Typed REST client for camera mutation and camera-discovery endpoints in the
// Admin API catalog. Camera list/detail reads remain on CameraClient.
class HUBSIGHT_ADMIN_EXPORT CameraManagementClient final : public QObject {
  Q_OBJECT

public:
  void create(const QJsonObject &camera);
  void patch(const QString &cameraId, const QJsonObject &changes);
  void remove(const QString &cameraId);

  void start(const QString &cameraId, const QJsonObject &options = {});
  void stop(const QString &cameraId, const QJsonObject &options = {});
  void restart(const QString &cameraId, const QJsonObject &options = {});

  void fetchThumbnail(const QString &cameraId);
  void fetchSnapshot(const QString &cameraId);
  void patchHomography(const QString &cameraId, const QJsonObject &homography);
  void movePtz(const QString &cameraId, const QJsonObject &movement);

  void listPresets(const QString &cameraId, const QString &cursor = {},
                   int limit = 50);
  void createPreset(const QString &cameraId, const QJsonObject &preset);

  void scanDiscovery(const QJsonObject &options = {});
  void fetchDiscoveryJob(const QString &jobId);
  void cancelDiscoveryJob(const QString &jobId);
  void probeOnvif(const QJsonObject &probe);

  void listRecognitionLogs(const QString &cameraId, const QString &cursor = {},
                           int limit = 50, const QJsonObject &filters = {});
  void
  clearRecognitionLogs(const QString &cameraId,
                       const QString &confirmation = QStringLiteral("yes"));

signals:
  void cameraCreated(HubSight::Admin::CameraSummary camera);
  void cameraPatched(QString cameraId, HubSight::Admin::CameraSummary camera);
  void cameraDeleted(QString cameraId,
                     HubSight::Admin::CameraActionResult result);
  void cameraStarted(QString cameraId,
                     HubSight::Admin::CameraActionResult result);
  void cameraStopped(QString cameraId,
                     HubSight::Admin::CameraActionResult result);
  void cameraRestarted(QString cameraId,
                       HubSight::Admin::CameraActionResult result);
  void thumbnailReceived(HubSight::Admin::CameraMedia media);
  void snapshotReceived(HubSight::Admin::CameraMedia media);
  void homographyPatched(QString cameraId,
                         HubSight::Admin::CameraHomography homography);
  void ptzMoved(QString cameraId, HubSight::Admin::CameraActionResult result);
  void presetsReceived(HubSight::Admin::CameraPresetPage page);
  void presetCreated(HubSight::Admin::CameraPreset preset);
  void discoveryScanStarted(HubSight::Admin::CameraDiscoveryJob job);
  void discoveryJobReceived(HubSight::Admin::CameraDiscoveryJob job);
  void discoveryJobCanceled(QString jobId,
                            HubSight::Admin::CameraActionResult result);
  void onvifProbeReceived(HubSight::Admin::CameraOnvifProbeResult result);
  void recognitionLogsReceived(HubSight::Admin::CameraRecognitionLogPage page);
  void recognitionLogsCleared(QString cameraId,
                              HubSight::Admin::CameraActionResult result);
  void operationCompleted(QString operation, QJsonObject response);
  void errorOccurred(HubSight::Admin::AdminError error);

private:
  enum class PendingKind {
    Create,
    Patch,
    Delete,
    Start,
    Stop,
    Restart,
    Thumbnail,
    Snapshot,
    Homography,
    PtzMove,
    PresetsList,
    PresetCreate,
    DiscoveryScan,
    DiscoveryJobGet,
    DiscoveryJobCancel,
    OnvifProbe,
    RecognitionLogsList,
    RecognitionLogsClear,
  };

  struct PendingRequest {
    PendingKind kind;
    QString cameraId;
    QString jobId;
  };

  friend class AdminClient;
  CameraManagementClient(AdminTransport *transport, QObject *parent = nullptr);

  void sendError(const AdminError &error);
  void sendRequest(PendingKind kind, const TransportRequest &request,
                   const QString &cameraId = {}, const QString &jobId = {});
  void handleResponse(quint64 requestId, const TransportResponse &response);

  AdminTransport *m_transport = nullptr;
  QHash<quint64, PendingRequest> m_pending;
};

} // namespace HubSight::Admin
