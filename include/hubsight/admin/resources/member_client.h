#pragma once

#include "../admin_export.h"
#include "../admin_types.h"
#include "member_types.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace HubSight::Admin {

class AdminTransport;
class AdminClient;
struct TransportRequest;
struct TransportResponse;

// Typed REST client for member, face, and image-upload endpoints in the Admin
// API catalog. Request DTOs remain QJsonObject-based because the
// server payload is intentionally extensible; response payloads are typed in
// member_types.h.
class HUBSIGHT_ADMIN_EXPORT MemberClient final : public QObject {
  Q_OBJECT

public:
  void listMembers(const QString &cursor = {}, int limit = 50,
                   const QJsonObject &filters = {});
  void createMember(const QJsonObject &member);
  void getMember(const QString &memberId);
  void patchMember(const QString &memberId, const QJsonObject &changes);
  void deleteMember(const QString &memberId);

  void putAvatar(const QString &memberId, const QJsonObject &avatar);
  // Sends the image bytes as the request body with the supplied media type.
  // This overload is for the Admin avatar endpoint's binary representation;
  // the JSON overload above remains available for metadata/presigned flows.
  void putAvatar(const QString &memberId, const QByteArray &image,
                 const QString &mimeType = QStringLiteral("image/jpeg"));
  void deleteAvatar(const QString &memberId);

  void listFaces(const QString &memberId, const QString &cursor = {},
                 int limit = 50, const QJsonObject &filters = {});
  void enrollFace(const QString &memberId, const QJsonObject &face);
  void deleteFace(const QString &memberId, const QString &faceId);
  void batchDeleteFaces(const QString &memberId, const QJsonObject &request);
  void batchDeleteFaces(const QString &memberId, const QStringList &faceIds);

  void uploadImages(const QJsonObject &request);
  // Sends one or more image parts as multipart/form-data. The SDK owns the
  // boundary and Content-Type construction; callers only provide bytes.
  void uploadImages(const QVector<ImageUploadPart> &parts,
                    const QJsonObject &metadata = {});
  void presignImages(const QJsonObject &request);

  // Explicit aliases for callers that prefer the full resource name in a
  // method call. They share the same transport operation and signals.
  void putMemberAvatar(const QString &memberId, const QJsonObject &avatar) {
    putAvatar(memberId, avatar);
  }
  void deleteMemberAvatar(const QString &memberId) { deleteAvatar(memberId); }
  void listMemberFaces(const QString &memberId, const QString &cursor = {},
                       int limit = 50, const QJsonObject &filters = {}) {
    listFaces(memberId, cursor, limit, filters);
  }
  void enrollMemberFace(const QString &memberId, const QJsonObject &face) {
    enrollFace(memberId, face);
  }
  void deleteMemberFace(const QString &memberId, const QString &faceId) {
    deleteFace(memberId, faceId);
  }
  void batchDeleteMemberFaces(const QString &memberId,
                              const QJsonObject &request) {
    batchDeleteFaces(memberId, request);
  }
  void batchDeleteMemberFaces(const QString &memberId,
                              const QStringList &faceIds) {
    batchDeleteFaces(memberId, faceIds);
  }
  void presign(const QJsonObject &request) { presignImages(request); }

signals:
  void membersReceived(HubSight::Admin::MemberPage page);
  void memberCreated(HubSight::Admin::Member member);
  void memberReceived(HubSight::Admin::Member member);
  void memberPatched(QString memberId, HubSight::Admin::Member member);
  void memberDeleted(QString memberId,
                     HubSight::Admin::MemberActionResult result);

  void avatarUpdated(QString memberId, HubSight::Admin::MemberAvatar avatar);
  void avatarDeleted(QString memberId,
                     HubSight::Admin::MemberActionResult result);

  void facesReceived(HubSight::Admin::FacePage page);
  void faceEnrolled(QString memberId, HubSight::Admin::Face face);
  void faceDeleted(QString memberId, QString faceId,
                   HubSight::Admin::MemberActionResult result);
  void facesBatchDeleted(QString memberId,
                         HubSight::Admin::MemberActionResult result);

  void imagesUploaded(HubSight::Admin::ImageUploadResult result);
  void imagesPresigned(HubSight::Admin::ImagePresignResult result);

  void operationCompleted(QString operation, QJsonObject response);
  void errorOccurred(HubSight::Admin::AdminError error);

private:
  enum class PendingKind {
    MembersList,
    MemberCreate,
    MemberGet,
    MemberPatch,
    MemberDelete,
    MemberAvatarPut,
    MemberAvatarDelete,
    MemberFacesList,
    MemberFacesEnroll,
    MemberFaceDelete,
    MemberFacesBatchDelete,
    UploadImages,
    UploadImagesPresign,
  };

  struct PendingRequest {
    PendingKind kind;
    QString memberId;
    QString faceId;
  };

  friend class AdminClient;
  MemberClient(AdminTransport *transport, QObject *parent = nullptr);

  void sendError(const AdminError &error);
  void sendRequest(PendingKind kind, const TransportRequest &request,
                   const QString &memberId = {}, const QString &faceId = {});
  void handleResponse(quint64 requestId, const TransportResponse &response);

  AdminTransport *m_transport = nullptr;
  QHash<quint64, PendingRequest> m_pending;
};

} // namespace HubSight::Admin
