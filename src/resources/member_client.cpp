#include "../../include/hubsight/admin/resources/member_client.h"

#include "../admin_transport.h"
#include "../resource_helpers.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

namespace HubSight::Admin {
namespace {

QByteArray jsonBody(const QJsonObject &object) {
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QString safeMultipartToken(QString value) {
  value.replace(QLatin1Char('\r'), QLatin1Char('_'));
  value.replace(QLatin1Char('\n'), QLatin1Char('_'));
  value.replace(QLatin1Char('"'), QLatin1Char('_'));
  return value.trimmed();
}

QByteArray multipartBody(const QVector<ImageUploadPart> &parts,
                         const QJsonObject &metadata, QByteArray *contentType) {
  const QByteArray boundary =
      QByteArrayLiteral("----HubSightAdminSdk-") +
      QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1();
  const QByteArray delimiter = QByteArrayLiteral("--") + boundary;
  QByteArray body;

  if (!metadata.isEmpty()) {
    body += delimiter + QByteArrayLiteral("\r\n");
    body += QByteArrayLiteral(
        "Content-Disposition: form-data; name=\"metadata\"\r\n");
    body += QByteArrayLiteral("Content-Type: application/json\r\n\r\n");
    body += jsonBody(metadata);
    body += QByteArrayLiteral("\r\n");
  }

  for (const ImageUploadPart &part : parts) {
    const QString fieldName = safeMultipartToken(
        part.fieldName.isEmpty() ? QStringLiteral("files") : part.fieldName);
    const QString fileName = safeMultipartToken(
        part.fileName.isEmpty() ? QStringLiteral("upload.bin") : part.fileName);
    const QString mimeType = safeMultipartToken(
        part.mimeType.isEmpty() ? QStringLiteral("application/octet-stream")
                                : part.mimeType);
    body += delimiter + QByteArrayLiteral("\r\n");
    body += QByteArrayLiteral("Content-Disposition: form-data; name=\"") +
            fieldName.toUtf8() + QByteArrayLiteral("\"; filename=\"") +
            fileName.toUtf8() + QByteArrayLiteral("\"\r\n");
    body += QByteArrayLiteral("Content-Type: ") + mimeType.toUtf8() +
            QByteArrayLiteral("\r\n\r\n");
    body += part.data;
    body += QByteArrayLiteral("\r\n");
  }

  body += delimiter + QByteArrayLiteral("--\r\n");
  *contentType = QByteArrayLiteral("multipart/form-data; boundary=") + boundary;
  return body;
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

QString memberPath(const QString &memberId) {
  return QStringLiteral("/members/") + encodedPathPart(memberId);
}

QString facePath(const QString &memberId, const QString &faceId) {
  return memberPath(memberId) + QStringLiteral("/faces/") +
         encodedPathPart(faceId);
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

QString listPath(const QString &basePath, const QString &cursor, int limit,
                 const QJsonObject &filters) {
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
  return basePath + QStringLiteral("?") + query.toString(QUrl::FullyEncoded);
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

} // namespace

MemberClient::MemberClient(AdminTransport *transport, QObject *parent)
    : QObject(parent), m_transport(transport) {
  qRegisterMetaType<Member>();
  qRegisterMetaType<MemberPage>();
  qRegisterMetaType<Face>();
  qRegisterMetaType<FacePage>();
  qRegisterMetaType<MemberActionResult>();
  qRegisterMetaType<MemberAvatar>();
  qRegisterMetaType<UploadedImage>();
  qRegisterMetaType<ImageUploadResult>();
  qRegisterMetaType<PresignedImage>();
  qRegisterMetaType<ImagePresignResult>();

  connect(m_transport, &AdminTransport::allRequestsCanceled, this,
          [this]() { m_pending.clear(); });
  connect(m_transport, &AdminTransport::finished, this,
          [this](quint64 requestId, const TransportResponse &response) {
            handleResponse(requestId, response);
          });
}

void MemberClient::listMembers(const QString &cursor, int limit,
                               const QJsonObject &filters) {
  const QString operation = QStringLiteral("members.list");
  if (limit < 1 || limit > 100) {
    sendError(invalidInput(
        operation,
        QStringLiteral("Member page limit must be between 1 and 100.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = listPath(QStringLiteral("/members"), cursor, limit, filters);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::MembersList, request);
}

void MemberClient::createMember(const QJsonObject &member) {
  const QString operation = QStringLiteral("members.create");
  if (member.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Member data is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/members");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(member);
  sendRequest(PendingKind::MemberCreate, request);
}

void MemberClient::getMember(const QString &memberId) {
  const QString operation = QStringLiteral("members.get");
  const QString normalizedId = memberId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Member ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = memberPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::MemberGet, request, normalizedId);
}

void MemberClient::patchMember(const QString &memberId,
                               const QJsonObject &changes) {
  const QString operation = QStringLiteral("members.patch");
  const QString normalizedId = memberId.trimmed();
  if (normalizedId.isEmpty() || changes.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("Member ID and changes are required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::CustomOperation;
  request.path = memberPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(changes);
  sendRequest(PendingKind::MemberPatch, request, normalizedId);
}

void MemberClient::deleteMember(const QString &memberId) {
  const QString operation = QStringLiteral("members.delete");
  const QString normalizedId = memberId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Member ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::DeleteOperation;
  request.path = memberPath(normalizedId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::MemberDelete, request, normalizedId);
}

void MemberClient::putAvatar(const QString &memberId,
                             const QJsonObject &avatar) {
  const QString operation = QStringLiteral("members.avatar.put");
  const QString normalizedId = memberId.trimmed();
  if (normalizedId.isEmpty() || avatar.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("Member ID and avatar data are required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PutOperation;
  request.path = memberPath(normalizedId) + QStringLiteral("/avatar");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(avatar);
  sendRequest(PendingKind::MemberAvatarPut, request, normalizedId);
}

void MemberClient::putAvatar(const QString &memberId, const QByteArray &image,
                             const QString &mimeType) {
  const QString operation = QStringLiteral("members.avatar.put");
  const QString normalizedId = memberId.trimmed();
  const QByteArray normalizedMimeType = mimeType.trimmed().toUtf8();
  if (normalizedId.isEmpty() || image.isEmpty() ||
      normalizedMimeType.isEmpty()) {
    sendError(invalidInput(
        operation,
        QStringLiteral("Member ID, image bytes, and MIME type are required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PutOperation;
  request.path = memberPath(normalizedId) + QStringLiteral("/avatar");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = image;
  request.contentType = normalizedMimeType;
  sendRequest(PendingKind::MemberAvatarPut, request, normalizedId);
}

void MemberClient::deleteAvatar(const QString &memberId) {
  const QString operation = QStringLiteral("members.avatar.delete");
  const QString normalizedId = memberId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Member ID is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::DeleteOperation;
  request.path = memberPath(normalizedId) + QStringLiteral("/avatar");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::MemberAvatarDelete, request, normalizedId);
}

void MemberClient::listFaces(const QString &memberId, const QString &cursor,
                             int limit, const QJsonObject &filters) {
  const QString operation = QStringLiteral("members.faces.list");
  const QString normalizedId = memberId.trimmed();
  if (normalizedId.isEmpty()) {
    sendError(
        invalidInput(operation, QStringLiteral("Member ID is required.")));
    return;
  }
  if (limit < 1 || limit > 100) {
    sendError(invalidInput(
        operation,
        QStringLiteral("Face page limit must be between 1 and 100.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::GetOperation;
  request.path = listPath(memberPath(normalizedId) + QStringLiteral("/faces"),
                          cursor, limit, filters);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::MemberFacesList, request, normalizedId);
}

void MemberClient::enrollFace(const QString &memberId,
                              const QJsonObject &face) {
  const QString operation = QStringLiteral("members.faces.enroll");
  const QString normalizedId = memberId.trimmed();
  if (normalizedId.isEmpty() || face.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("Member ID and face data are required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = memberPath(normalizedId) + QStringLiteral("/faces:enroll");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(face);
  sendRequest(PendingKind::MemberFacesEnroll, request, normalizedId);
}

void MemberClient::deleteFace(const QString &memberId, const QString &faceId) {
  const QString operation = QStringLiteral("members.face.delete");
  const QString normalizedMemberId = memberId.trimmed();
  const QString normalizedFaceId = faceId.trimmed();
  if (normalizedMemberId.isEmpty() || normalizedFaceId.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("Member ID and face ID are required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::DeleteOperation;
  request.path = facePath(normalizedMemberId, normalizedFaceId);
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  sendRequest(PendingKind::MemberFaceDelete, request, normalizedMemberId,
              normalizedFaceId);
}

void MemberClient::batchDeleteFaces(const QString &memberId,
                                    const QJsonObject &requestBody) {
  const QString operation = QStringLiteral("members.faces.batch_delete");
  const QString normalizedId = memberId.trimmed();
  if (normalizedId.isEmpty() || requestBody.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("Member ID and face data are required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path =
      memberPath(normalizedId) + QStringLiteral("/faces:batch-delete");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(requestBody);
  sendRequest(PendingKind::MemberFacesBatchDelete, request, normalizedId);
}

void MemberClient::batchDeleteFaces(const QString &memberId,
                                    const QStringList &faceIds) {
  QJsonArray ids;
  for (const QString &faceId : faceIds) {
    const QString normalizedId = faceId.trimmed();
    if (!normalizedId.isEmpty() && !ids.contains(normalizedId)) {
      ids.append(normalizedId);
    }
  }
  if (ids.isEmpty()) {
    sendError(
        invalidInput(QStringLiteral("members.faces.batch_delete"),
                     QStringLiteral("At least one face ID is required.")));
    return;
  }
  batchDeleteFaces(memberId, QJsonObject{{QStringLiteral("face_ids"), ids}});
}

void MemberClient::uploadImages(const QJsonObject &requestBody) {
  const QString operation = QStringLiteral("uploads.images");
  if (requestBody.isEmpty()) {
    sendError(invalidInput(operation,
                           QStringLiteral("Image upload data is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/uploads/images");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(requestBody);
  sendRequest(PendingKind::UploadImages, request);
}

void MemberClient::uploadImages(const QVector<ImageUploadPart> &parts,
                                const QJsonObject &metadata) {
  const QString operation = QStringLiteral("uploads.images");
  if (parts.isEmpty()) {
    sendError(invalidInput(
        operation, QStringLiteral("At least one image part is required.")));
    return;
  }
  for (const ImageUploadPart &part : parts) {
    if (!part.isValid()) {
      sendError(invalidInput(
          operation,
          QStringLiteral("Every image part must contain non-empty bytes.")));
      return;
    }
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/uploads/images");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = multipartBody(parts, metadata, &request.contentType);
  sendRequest(PendingKind::UploadImages, request);
}

void MemberClient::presignImages(const QJsonObject &requestBody) {
  const QString operation = QStringLiteral("uploads.images.presign");
  if (requestBody.isEmpty()) {
    sendError(invalidInput(operation,
                           QStringLiteral("Image presign data is required.")));
    return;
  }

  TransportRequest request;
  request.method = QNetworkAccessManager::PostOperation;
  request.path = QStringLiteral("/uploads/images:presign");
  request.operation = operation;
  request.authentication = RequestAuth::Protected;
  request.body = jsonBody(requestBody);
  sendRequest(PendingKind::UploadImagesPresign, request);
}

void MemberClient::sendError(const AdminError &error) {
  emit errorOccurred(error);
}

void MemberClient::sendRequest(PendingKind kind,
                               const TransportRequest &request,
                               const QString &memberId, const QString &faceId) {
  const quint64 requestId = m_transport->send(request);
  if (requestId == 0) {
    sendError(m_transport->configurationError(request.operation));
    return;
  }
  m_pending.insert(requestId, {kind, memberId, faceId});
}

void MemberClient::handleResponse(quint64 requestId,
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

  const auto actionResult = [&]() {
    MemberActionResult result = MemberActionResult::fromJson(
        object, pendingRequest.memberId, pendingRequest.faceId);
    result.success = true;
    return result;
  };

  switch (pendingRequest.kind) {
  case PendingKind::MembersList: {
    const MemberPage page = MemberPage::fromJson(object);
    if (!page.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("member page")));
      return;
    }
    emit membersReceived(page);
    break;
  }
  case PendingKind::MemberCreate: {
    const Member member = Member::fromJson(object);
    if (!member.isValid()) {
      sendError(
          Internal::invalidModel(response.operation, QStringLiteral("member")));
      return;
    }
    emit memberCreated(member);
    break;
  }
  case PendingKind::MemberGet: {
    Member member = Member::fromJson(object);
    if (member.id.isEmpty()) {
      member.id = pendingRequest.memberId;
    }
    if (!member.isValid()) {
      sendError(
          Internal::invalidModel(response.operation, QStringLiteral("member")));
      return;
    }
    emit memberReceived(member);
    break;
  }
  case PendingKind::MemberPatch: {
    Member member = Member::fromJson(object);
    if (member.id.isEmpty()) {
      member.id = pendingRequest.memberId;
    }
    if (!member.isValid()) {
      sendError(
          Internal::invalidModel(response.operation, QStringLiteral("member")));
      return;
    }
    emit memberPatched(pendingRequest.memberId, member);
    break;
  }
  case PendingKind::MemberDelete:
    emit memberDeleted(pendingRequest.memberId, actionResult());
    break;
  case PendingKind::MemberAvatarPut: {
    const MemberAvatar avatar =
        MemberAvatar::fromJson(object, pendingRequest.memberId);
    if (!avatar.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("member avatar")));
      return;
    }
    emit avatarUpdated(pendingRequest.memberId, avatar);
    break;
  }
  case PendingKind::MemberAvatarDelete:
    emit avatarDeleted(pendingRequest.memberId, actionResult());
    break;
  case PendingKind::MemberFacesList: {
    const FacePage page = FacePage::fromJson(object, pendingRequest.memberId);
    if (!page.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("face page")));
      return;
    }
    emit facesReceived(page);
    break;
  }
  case PendingKind::MemberFacesEnroll: {
    const Face face = Face::fromJson(object, pendingRequest.memberId);
    if (!face.isValid()) {
      sendError(
          Internal::invalidModel(response.operation, QStringLiteral("face")));
      return;
    }
    emit faceEnrolled(pendingRequest.memberId, face);
    break;
  }
  case PendingKind::MemberFaceDelete:
    emit faceDeleted(pendingRequest.memberId, pendingRequest.faceId,
                     actionResult());
    break;
  case PendingKind::MemberFacesBatchDelete:
    emit facesBatchDeleted(pendingRequest.memberId, actionResult());
    break;
  case PendingKind::UploadImages: {
    const ImageUploadResult result = ImageUploadResult::fromJson(object);
    if (!result.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("image upload result")));
      return;
    }
    emit imagesUploaded(result);
    break;
  }
  case PendingKind::UploadImagesPresign: {
    const ImagePresignResult result = ImagePresignResult::fromJson(object);
    if (!result.isValid()) {
      sendError(Internal::invalidModel(response.operation,
                                       QStringLiteral("image presign result")));
      return;
    }
    emit imagesPresigned(result);
    break;
  }
  }
}

} // namespace HubSight::Admin
