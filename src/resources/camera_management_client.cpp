#include "../../include/hubsight/admin/resources/camera_management_client.h"

#include "../admin_transport.h"
#include "../resource_helpers.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QUrlQuery>

#include <initializer_list>

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

AdminError withContext(AdminError error, const QString &cameraId,
                       const QString &jobId = {}) {
  QJsonObject details = error.details.toObject();
  if (!error.details.isUndefined() && !error.details.isNull() &&
      !error.details.isObject()) {
    details.insert(QStringLiteral("server_details"), error.details);
  }
  if (!cameraId.isEmpty()) {
    details.insert(QStringLiteral("camera_id"), cameraId);
  }
  if (!jobId.isEmpty()) {
    details.insert(QStringLiteral("job_id"), jobId);
  }
  if (!details.isEmpty()) {
    error.details = details;
  }
  return error;
}

QString encodedPathPart(const QString &value) {
  return QString::fromUtf8(QUrl::toPercentEncoding(value.trimmed()));
}

QString cameraPath(const QString &cameraId) {
  return QStringLiteral("/cameras/") + encodedPathPart(cameraId);
}

QString cameraActionPath(const QString &cameraId, const QString &action) {
  return cameraPath(cameraId) + QStringLiteral(":") + action;
}

QString presetsPath(const QString &cameraId) {
  return cameraPath(cameraId) + QStringLiteral("/presets");
}

QString discoveryJobPath(const QString &jobId) {
  return QStringLiteral("/camera-discovery/jobs/") + encodedPathPart(jobId);
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

bool decodeResponse(const QByteArray &body, QJsonObject *object) {
  if (body.trimmed().isEmpty()) {
    *object = {};
    return true;
  }

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

QJsonObject cameraObject(const QJsonObject &json) {
  QJsonObject source = json;
  const QJsonValue data = source.value(QStringLiteral("data"));
  if (data.isObject()) {
    source = data.toObject();
  }
  for (const QString &key :
       {QStringLiteral("camera"), QStringLiteral("camera_data")}) {
    const QJsonValue value = source.value(key);
    if (value.isObject()) {
      return value.toObject();
    }
  }
  return source;
}

QJsonObject confirmationBody(const QString &confirmation) {
  return QJsonObject{{QStringLiteral("confirmation"), confirmation.trimmed()}};
}

} // namespace

CameraManagementClient::CameraManagementClient(AdminTransport *transport,
                                               QObject *parent)
    : QObject(parent), m_transport(transport) {
  qRegisterMetaType<CameraActionResult>();
  qRegisterMetaType<CameraMedia>();
  qRegisterMetaType<CameraHomography>();
  qRegisterMetaType<CameraPreset>();
  qRegisterMetaType<CameraPresetPage>();
  qRegisterMetaType<CameraDiscoveryJob>();
  qRegisterMetaType<CameraOnvifProbeResult>();
  qRegisterMetaType<CameraRecognitionLog>();
  qRegisterMetaType<CameraRecognitionLogPage>();

  connect(m_transport, &AdminTransport::allRequestsCanceled, this,
          [this]() { m_pending.clear(); });
  connect(m_transport, &AdminTransport::finished, this,
          [this](quint64 requestId, const TransportResponse &response) {
            handleResponse(requestId, response);
          });
}

void CameraManagementClient::create(const QJsonObject &camera) {
  const QString operation = QStringLiteral("cameras.create");
  if (camera.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Camera data is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/cameras");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(camera);
  sendRequest(PendingKind::Create, request);
}

void CameraManagementClient::patch(const QString &cameraId,
                                   const QJsonObject &changes) {
  const QString operation = QStringLiteral("cameras.patch");
  const QString normalizedId = cameraId.trimmed();
  if (normalizedId.isEmpty() || changes.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("Camera ID and changes are required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::CustomOperation;
  request.path = cameraPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(changes);
  sendRequest(PendingKind::Patch, request, normalizedId);
}

void CameraManagementClient::remove(const QString &cameraId) {
  const QString operation = QStringLiteral("cameras.delete");
  const QString normalizedId = cameraId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Camera ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::DeleteOperation;
  request.path = cameraPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::Delete, request, normalizedId);
}

void CameraManagementClient::start(const QString &cameraId,
                                   const QJsonObject &options) {
  const QString operation = QStringLiteral("cameras.start");
  const QString normalizedId = cameraId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Camera ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = cameraActionPath(normalizedId, QStringLiteral("start"));
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::Start, request, normalizedId);
}

void CameraManagementClient::stop(const QString &cameraId,
                                  const QJsonObject &options) {
  const QString operation = QStringLiteral("cameras.stop");
  const QString normalizedId = cameraId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Camera ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = cameraActionPath(normalizedId, QStringLiteral("stop"));
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::Stop, request, normalizedId);
}

void CameraManagementClient::restart(const QString &cameraId,
                                     const QJsonObject &options) {
  const QString operation = QStringLiteral("cameras.restart");
  const QString normalizedId = cameraId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Camera ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = cameraActionPath(normalizedId, QStringLiteral("restart"));
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::Restart, request, normalizedId);
}

void CameraManagementClient::fetchThumbnail(const QString &cameraId) {
  const QString operation = QStringLiteral("cameras.thumbnail");
  const QString normalizedId = cameraId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Camera ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = cameraPath(normalizedId) + QStringLiteral("/thumbnail");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::Thumbnail, request, normalizedId);
}

void CameraManagementClient::fetchSnapshot(const QString &cameraId) {
  const QString operation = QStringLiteral("cameras.snapshot");
  const QString normalizedId = cameraId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Camera ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = cameraPath(normalizedId) + QStringLiteral("/snapshot");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::Snapshot, request, normalizedId);
}

void CameraManagementClient::patchHomography(const QString &cameraId,
                                             const QJsonObject &homography) {
  const QString operation = QStringLiteral("cameras.homography.patch");
  const QString normalizedId = cameraId.trimmed();
  if (normalizedId.isEmpty() || homography.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("Camera ID and homography are required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::CustomOperation;
  request.path = cameraPath(normalizedId) + QStringLiteral("/homography");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(homography);
  sendRequest(PendingKind::Homography, request, normalizedId);
}

void CameraManagementClient::movePtz(const QString &cameraId,
                                     const QJsonObject &movement) {
  const QString operation = QStringLiteral("cameras.ptz.move");
  const QString normalizedId = cameraId.trimmed();
  if (normalizedId.isEmpty() || movement.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("Camera ID and PTZ movement are required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = cameraActionPath(normalizedId, QStringLiteral("ptz:move"));
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(movement);
  sendRequest(PendingKind::PtzMove, request, normalizedId);
}

void CameraManagementClient::listPresets(const QString &cameraId,
                                         const QString &cursor, int limit) {
  const QString operation = QStringLiteral("cameras.presets.list");
  const QString normalizedId = cameraId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Camera ID is required.")));
    return;
  }
  if (limit < 1 || limit > 100) {
    sendError(invalidInput(
        operation, QStringLiteral("Preset page limit must be between 1 and "
                                  "100.")));
    return;
  }

  QUrlQuery query;
  query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
  if (!cursor.trimmed().isEmpty()) {
    query.addQueryItem(QStringLiteral("cursor"), cursor.trimmed());
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = presetsPath(normalizedId) + QStringLiteral("?") +
                 query.toString(QUrl::FullyEncoded);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::PresetsList, request, normalizedId);
}

void CameraManagementClient::createPreset(const QString &cameraId,
                                          const QJsonObject &preset) {
  const QString operation = QStringLiteral("cameras.presets.create");
  const QString normalizedId = cameraId.trimmed();
  if (normalizedId.isEmpty() || preset.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("Camera ID and preset data are required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = presetsPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(preset);
  sendRequest(PendingKind::PresetCreate, request, normalizedId);
}

void CameraManagementClient::scanDiscovery(const QJsonObject &options) {
  const QString operation = QStringLiteral("camera.discovery.scan");
  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/camera-discovery:scan");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(options);
  sendRequest(PendingKind::DiscoveryScan, request);
}

void CameraManagementClient::fetchDiscoveryJob(const QString &jobId) {
  const QString operation = QStringLiteral("camera.discovery.job.get");
  const QString normalizedId = jobId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(invalidInput(operation, QStringLiteral("Job ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = discoveryJobPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::DiscoveryJobGet, request, {}, normalizedId);
}

void CameraManagementClient::cancelDiscoveryJob(const QString &jobId) {
  const QString operation = QStringLiteral("camera.discovery.job.cancel");
  const QString normalizedId = jobId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(invalidInput(operation, QStringLiteral("Job ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = discoveryJobPath(normalizedId) + QStringLiteral(":cancel");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::DiscoveryJobCancel, request, {}, normalizedId);
}

void CameraManagementClient::probeOnvif(const QJsonObject &probe) {
  const QString operation = QStringLiteral("cameras.onvif.probe");
  if (probe.isEmpty()) {
    sendError(invalidInput(operation,
                           QStringLiteral("ONVIF probe data is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/cameras:onvif-probe");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(probe);
  sendRequest(PendingKind::OnvifProbe, request);
}

void CameraManagementClient::listRecognitionLogs(const QString &cameraId,
                                                 const QString &cursor,
                                                 int limit,
                                                 const QJsonObject &filters) {
  const QString operation = QStringLiteral("cameras.recognition_logs.list");
  const QString normalizedId = cameraId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Camera ID is required.")));
    return;
  }
  if (limit < 1 || limit > 100) {
    sendError(invalidInput(
        operation, QStringLiteral("Recognition log page limit must be between "
                                  "1 and 100.")));
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
  request.path = cameraPath(normalizedId) +
                 QStringLiteral("/recognition-logs?") +
                 query.toString(QUrl::FullyEncoded);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::RecognitionLogsList, request, normalizedId);
}

void CameraManagementClient::clearRecognitionLogs(const QString &cameraId,
                                                  const QString &confirmation) {
  const QString operation = QStringLiteral("cameras.recognition_logs.clear");
  const QString normalizedId = cameraId.trimmed();
  const QString normalizedConfirmation = confirmation.trimmed();
  if (normalizedId.isEmpty() || normalizedConfirmation.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("Camera ID and confirmation are required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::DeleteOperation;
  request.path = cameraPath(normalizedId) + QStringLiteral("/recognition-logs");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(confirmationBody(normalizedConfirmation));
  sendRequest(PendingKind::RecognitionLogsClear, request, normalizedId);
}

void CameraManagementClient::sendError(const AdminError &error) {
  emit errorOccurred(error);
}

void CameraManagementClient::sendRequest(PendingKind kind,
                                         const TransportRequest &request,
                                         const QString &cameraId,
                                         const QString &jobId) {
  const quint64 requestId = m_transport->send(request);
  if (requestId == 0) {
    sendError(withContext(m_transport->configurationError(request.operation),
                          cameraId, jobId));
    return;
  }
  m_pending.insert(requestId, {kind, cameraId, jobId});
}

void CameraManagementClient::handleResponse(quint64 requestId,
                                            const TransportResponse &response) {
  const auto pending = m_pending.find(requestId);
  if (pending == m_pending.end()) {
    return;
  }
  const PendingRequest pendingRequest = pending.value();
  m_pending.erase(pending);

  if (!response.isHttpSuccess()) {
    sendError(withContext(
        Internal::parseError(m_transport, response, response.operation),
        pendingRequest.cameraId, pendingRequest.jobId));
    return;
  }

  QJsonObject object;
  bool validJson = decodeResponse(response.body, &object);
  const bool mediaRequest = pendingRequest.kind == PendingKind::Thumbnail ||
                            pendingRequest.kind == PendingKind::Snapshot;
  if (mediaRequest) {
    CameraMedia media;
    if (validJson && !object.isEmpty()) {
      media = CameraMedia::fromJson(object, pendingRequest.cameraId);
    } else if (!response.body.isEmpty()) {
      media.cameraId = pendingRequest.cameraId;
      media.bytes = response.body;
    }

    if (!media.isValid()) {
      sendError(withContext(
          validJson ? Internal::invalidModel(response.operation,
                                             QStringLiteral("camera media"))
                    : Internal::invalidJson(response.operation),
          pendingRequest.cameraId));
      return;
    }

    emit operationCompleted(response.operation,
                            validJson ? object : QJsonObject{});
    if (pendingRequest.kind == PendingKind::Thumbnail) {
      emit thumbnailReceived(media);
    } else {
      emit snapshotReceived(media);
    }
    return;
  }

  if (!validJson) {
    sendError(withContext(Internal::invalidJson(response.operation),
                          pendingRequest.cameraId, pendingRequest.jobId));
    return;
  }
  emit operationCompleted(response.operation, object);

  switch (pendingRequest.kind) {
  case PendingKind::Create: {
    const CameraSummary camera = CameraSummary::fromJson(cameraObject(object));
    if (!camera.isValid()) {
      sendError(
          Internal::invalidModel(response.operation, QStringLiteral("camera")));
      return;
    }
    emit cameraCreated(camera);
    break;
  }
  case PendingKind::Patch: {
    const CameraSummary camera = CameraSummary::fromJson(cameraObject(object));
    if (!camera.isValid()) {
      sendError(withContext(
          Internal::invalidModel(response.operation, QStringLiteral("camera")),
          pendingRequest.cameraId));
      return;
    }
    emit cameraPatched(pendingRequest.cameraId, camera);
    break;
  }
  case PendingKind::Delete:
    emit cameraDeleted(
        pendingRequest.cameraId,
        CameraActionResult::fromJson(object, pendingRequest.cameraId));
    break;
  case PendingKind::Start:
    emit cameraStarted(
        pendingRequest.cameraId,
        CameraActionResult::fromJson(object, pendingRequest.cameraId));
    break;
  case PendingKind::Stop:
    emit cameraStopped(
        pendingRequest.cameraId,
        CameraActionResult::fromJson(object, pendingRequest.cameraId));
    break;
  case PendingKind::Restart:
    emit cameraRestarted(
        pendingRequest.cameraId,
        CameraActionResult::fromJson(object, pendingRequest.cameraId));
    break;
  case PendingKind::Homography: {
    const CameraHomography homography =
        CameraHomography::fromJson(object, pendingRequest.cameraId);
    if (!homography.isValid()) {
      sendError(
          withContext(Internal::invalidModel(response.operation,
                                             QStringLiteral("homography")),
                      pendingRequest.cameraId));
      return;
    }
    emit homographyPatched(pendingRequest.cameraId, homography);
    break;
  }
  case PendingKind::PtzMove:
    emit ptzMoved(
        pendingRequest.cameraId,
        CameraActionResult::fromJson(object, pendingRequest.cameraId));
    break;
  case PendingKind::PresetsList: {
    const CameraPresetPage page =
        CameraPresetPage::fromJson(object, pendingRequest.cameraId);
    if (!page.isValid()) {
      sendError(
          withContext(Internal::invalidModel(response.operation,
                                             QStringLiteral("preset page")),
                      pendingRequest.cameraId));
      return;
    }
    emit presetsReceived(page);
    break;
  }
  case PendingKind::PresetCreate: {
    const CameraPreset preset =
        CameraPreset::fromJson(object, pendingRequest.cameraId);
    if (!preset.isValid()) {
      sendError(withContext(
          Internal::invalidModel(response.operation, QStringLiteral("preset")),
          pendingRequest.cameraId));
      return;
    }
    emit presetCreated(preset);
    break;
  }
  case PendingKind::DiscoveryScan: {
    const CameraDiscoveryJob job = CameraDiscoveryJob::fromJson(object);
    if (!job.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("camera discovery job")));
      return;
    }
    emit discoveryScanStarted(job);
    break;
  }
  case PendingKind::DiscoveryJobGet: {
    const CameraDiscoveryJob job =
        CameraDiscoveryJob::fromJson(object, pendingRequest.jobId);
    if (!job.isValid()) {
      sendError(withContext(
          Internal::invalidModel(response.operation,
                                 QStringLiteral("camera discovery job")),
          {}, pendingRequest.jobId));
      return;
    }
    emit discoveryJobReceived(job);
    break;
  }
  case PendingKind::DiscoveryJobCancel:
    emit discoveryJobCanceled(
        pendingRequest.jobId,
        CameraActionResult::fromJson(object, {}, pendingRequest.jobId));
    break;
  case PendingKind::OnvifProbe: {
    const CameraOnvifProbeResult result =
        CameraOnvifProbeResult::fromJson(object);
    if (!result.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("ONVIF probe")));
      return;
    }
    emit onvifProbeReceived(result);
    break;
  }
  case PendingKind::RecognitionLogsList: {
    const CameraRecognitionLogPage page =
        CameraRecognitionLogPage::fromJson(object, pendingRequest.cameraId);
    if (!page.isValid()) {
      sendError(withContext(
          Internal::invalidModel(response.operation,
                                 QStringLiteral("recognition log page")),
          pendingRequest.cameraId));
      return;
    }
    emit recognitionLogsReceived(page);
    break;
  }
  case PendingKind::RecognitionLogsClear:
    emit recognitionLogsCleared(
        pendingRequest.cameraId,
        CameraActionResult::fromJson(object, pendingRequest.cameraId));
    break;
  case PendingKind::Thumbnail:
  case PendingKind::Snapshot:
    // Media requests return from the branch above.
    break;
  }
}

} // namespace HubSight::Admin
