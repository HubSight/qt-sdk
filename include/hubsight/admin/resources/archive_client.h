#pragma once

#include "../admin_export.h"
#include "../admin_types.h"
#include "archive_types.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace HubSight::Admin {

class AdminTransport;
class AdminClient;
struct TransportRequest;
struct TransportResponse;

// Typed REST client for the Admin archive/timeline contract. It deliberately
// uses the normative /archive/recordings action endpoints; legacy redirect
// routes exposed by older backends are not substituted here.
class HUBSIGHT_ADMIN_EXPORT ArchiveClient final : public QObject {
  Q_OBJECT

public:
  // from/to are required UTC-normalizable timestamps. Query values are sent
  // as ISO-8601 strings under the normative Admin archive contract.
  void fetchTimeline(const QDateTime &from, const QDateTime &to,
                     const QString &cameraId = {}, const QString &cursor = {},
                     int limit = 100);
  void fetchAvailableDays(const QString &cameraId, int year, int month);
  void fetchRecording(const QString &recordingId);
  void requestPlaybackUrl(const QString &recordingId,
                          const QJsonObject &options = {});
  void requestDownloadUrl(const QString &recordingId,
                          const QJsonObject &options = {});
  void requestThumbnailUrl(const QString &recordingId,
                           const QJsonObject &options = {});

signals:
  void timelineReceived(HubSight::Admin::ArchiveTimelinePage page);
  void availableDaysReceived(HubSight::Admin::ArchiveAvailableDays days);
  void recordingReceived(HubSight::Admin::RecordingSegment recording);
  void playbackUrlReceived(HubSight::Admin::ArchiveUrlResult result);
  void downloadUrlReceived(HubSight::Admin::ArchiveUrlResult result);
  void thumbnailUrlReceived(HubSight::Admin::ArchiveUrlResult result);
  void operationCompleted(QString operation, QJsonObject response);
  void errorOccurred(HubSight::Admin::AdminError error);

private:
  enum class PendingKind {
    Timeline,
    AvailableDays,
    Recording,
    PlaybackUrl,
    DownloadUrl,
    ThumbnailUrl,
  };

  struct PendingRequest {
    PendingKind kind;
    QString recordingId;
    QString cameraId;
    int year = 0;
    int month = 0;
  };

  friend class AdminClient;
  ArchiveClient(AdminTransport *transport, QObject *parent = nullptr);

  void sendError(const AdminError &error);
  void sendRequest(PendingKind kind, const TransportRequest &request,
                   const PendingRequest &pending);
  void handleResponse(quint64 requestId, const TransportResponse &response);

  AdminTransport *m_transport = nullptr;
  QHash<quint64, PendingRequest> m_pending;
};

} // namespace HubSight::Admin
