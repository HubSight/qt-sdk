#include "../../include/hubsight/admin/resources/archive_client.h"

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

QString recordingPath(const QString &recordingId) {
  return QStringLiteral("/archive/recordings/") + encodedPathPart(recordingId);
}

QString availableDaysPath(const QString &cameraId) {
  return QStringLiteral("/archive/cameras/") + encodedPathPart(cameraId) +
         QStringLiteral("/available-days");
}

QString iso8601(const QDateTime &value) {
  return value.toUTC().toString(Qt::ISODateWithMs);
}

bool decodeResponse(const QByteArray &body, QJsonObject *object) {
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
  if (document.isNull()) {
    return false;
  }
  if (document.isObject()) {
    *object = document.object();
    return true;
  }
  if (document.isArray()) {
    *object = QJsonObject{{QStringLiteral("items"), document.array()}};
    return true;
  }
  return false;
}

} // namespace

ArchiveClient::ArchiveClient(AdminTransport *transport, QObject *parent)
    : QObject(parent), m_transport(transport) {
  qRegisterMetaType<RecordingSegment>();
  qRegisterMetaType<ArchiveTimelinePage>();
  qRegisterMetaType<ArchiveAvailableDays>();
  qRegisterMetaType<ArchiveUrlResult>();

  connect(m_transport, &AdminTransport::allRequestsCanceled, this,
          [this]() { m_pending.clear(); });
  connect(m_transport, &AdminTransport::finished, this,
          [this](quint64 requestId, const TransportResponse &response) {
            handleResponse(requestId, response);
          });
}

void ArchiveClient::fetchTimeline(const QDateTime &from, const QDateTime &to,
                                  const QString &cameraId,
                                  const QString &cursor, int limit) {
  const QString operation = QStringLiteral("archive.timeline");
  if (!from.isValid() || !to.isValid() || from >= to) {
    sendError(invalidInput(
        operation, QStringLiteral("A valid from/to interval is required.")));
    return;
  }
  if (limit < 1 || limit > 500) {
    sendError(invalidInput(
        operation, QStringLiteral("Archive timeline limit must be between 1 "
                                  "and 500.")));
    return;
  }

  QUrlQuery query;
  query.addQueryItem(QStringLiteral("from"), iso8601(from));
  query.addQueryItem(QStringLiteral("to"), iso8601(to));
  query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
  if (!cameraId.trimmed().isEmpty()) {
    query.addQueryItem(QStringLiteral("camera_id"), cameraId.trimmed());
  }
  if (!cursor.trimmed().isEmpty()) {
    query.addQueryItem(QStringLiteral("cursor"), cursor.trimmed());
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path =
      QStringLiteral("/archive/timeline?") + query.toString(QUrl::FullyEncoded);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::Timeline, request, {});
}

void ArchiveClient::fetchAvailableDays(const QString &cameraId, int year,
                                       int month) {
  const QString operation = QStringLiteral("archive.available_days");
  const QString normalizedCameraId = cameraId.trimmed();
  if (normalizedCameraId.isEmpty() || year < 1 || month < 1 || month > 12) {
    sendError(invalidInput(
        operation, QStringLiteral("Camera ID, year, and a month from 1 to 12 "
                                  "are required.")));
    return;
  }

  QUrlQuery query;
  query.addQueryItem(QStringLiteral("year"), QString::number(year));
  query.addQueryItem(QStringLiteral("month"), QString::number(month));

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = availableDaysPath(normalizedCameraId) + QStringLiteral("?") +
                 query.toString(QUrl::FullyEncoded);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(
      PendingKind::AvailableDays, request,
      {PendingKind::AvailableDays, {}, normalizedCameraId, year, month});
}

void ArchiveClient::fetchRecording(const QString &recordingId) {
  const QString operation = QStringLiteral("archive.recording.get");
  const QString normalizedRecordingId = recordingId.trimmed();
  if (normalizedRecordingId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Recording ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = recordingPath(normalizedRecordingId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::Recording, request,
              {PendingKind::Recording, normalizedRecordingId});
}

void ArchiveClient::requestPlaybackUrl(const QString &recordingId,
                                       const QJsonObject &options) {
  const QString operation = QStringLiteral("archive.recording.playback_url");
  const QString normalizedRecordingId = recordingId.trimmed();
  if (normalizedRecordingId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Recording ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path =
      recordingPath(normalizedRecordingId) + QStringLiteral(":playback-url");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::PlaybackUrl, request,
              {PendingKind::PlaybackUrl, normalizedRecordingId});
}

void ArchiveClient::requestDownloadUrl(const QString &recordingId,
                                       const QJsonObject &options) {
  const QString operation = QStringLiteral("archive.recording.download_url");
  const QString normalizedRecordingId = recordingId.trimmed();
  if (normalizedRecordingId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Recording ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path =
      recordingPath(normalizedRecordingId) + QStringLiteral(":download-url");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::DownloadUrl, request,
              {PendingKind::DownloadUrl, normalizedRecordingId});
}

void ArchiveClient::requestThumbnailUrl(const QString &recordingId,
                                        const QJsonObject &options) {
  const QString operation = QStringLiteral("archive.recording.thumbnail_url");
  const QString normalizedRecordingId = recordingId.trimmed();
  if (normalizedRecordingId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Recording ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path =
      recordingPath(normalizedRecordingId) + QStringLiteral(":thumbnail-url");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::ThumbnailUrl, request,
              {PendingKind::ThumbnailUrl, normalizedRecordingId});
}

void ArchiveClient::sendError(const AdminError &error) {
  emit errorOccurred(error);
}

void ArchiveClient::sendRequest(PendingKind kind,
                                const TransportRequest &request,
                                const PendingRequest &pending) {
  const quint64 requestId = m_transport->send(request);
  if (requestId == 0) {
    sendError(m_transport->configurationError(request.operation));
    return;
  }
  PendingRequest requestState = pending;
  requestState.kind = kind;
  m_pending.insert(requestId, requestState);
}

void ArchiveClient::handleResponse(quint64 requestId,
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
  case PendingKind::Timeline: {
    const ArchiveTimelinePage page = ArchiveTimelinePage::fromJson(object);
    if (!page.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("archive timeline")));
      return;
    }
    emit timelineReceived(page);
    break;
  }
  case PendingKind::AvailableDays: {
    ArchiveAvailableDays days = ArchiveAvailableDays::fromJson(object);
    if (days.cameraId.isEmpty()) {
      days.cameraId = pendingRequest.cameraId;
    }
    if (days.year == 0) {
      days.year = pendingRequest.year;
    }
    if (days.month == 0) {
      days.month = pendingRequest.month;
    }
    if (!days.isValid()) {
      sendError(Internal::invalidModel(
          response.operation, QStringLiteral("archive available days")));
      return;
    }
    emit availableDaysReceived(days);
    break;
  }
  case PendingKind::Recording: {
    RecordingSegment recording = RecordingSegment::fromJson(object);
    if (recording.id.isEmpty()) {
      recording.id = pendingRequest.recordingId;
    }
    if (!recording.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("archive recording")));
      return;
    }
    emit recordingReceived(recording);
    break;
  }
  case PendingKind::PlaybackUrl:
  case PendingKind::DownloadUrl:
  case PendingKind::ThumbnailUrl: {
    const ArchiveUrlResult result =
        ArchiveUrlResult::fromJson(object, pendingRequest.recordingId);
    if (!result.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("archive URL result")));
      return;
    }
    if (pendingRequest.kind == PendingKind::PlaybackUrl) {
      emit playbackUrlReceived(result);
    } else if (pendingRequest.kind == PendingKind::DownloadUrl) {
      emit downloadUrlReceived(result);
    } else {
      emit thumbnailUrlReceived(result);
    }
    break;
  }
  }
}

} // namespace HubSight::Admin
