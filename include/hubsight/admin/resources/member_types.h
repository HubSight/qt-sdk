#pragma once

#include "../admin_export.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace HubSight::Admin {

struct HUBSIGHT_ADMIN_EXPORT ImageUploadPart {
  QString fieldName = QStringLiteral("files");
  QString fileName;
  QString mimeType = QStringLiteral("application/octet-stream");
  QByteArray data;

  bool isValid() const { return !data.isEmpty(); }
};

struct HUBSIGHT_ADMIN_EXPORT Member {
  QString id;
  QString name;
  QString displayName;
  QString firstName;
  QString lastName;
  QString email;
  QString phone;
  QString externalId;
  QString status;
  QString avatarUrl;
  QStringList tags;
  int faceCount = 0;
  bool active = true;
  QDateTime createdAt;
  QDateTime updatedAt;
  QJsonObject metadata;
  QJsonObject raw;

  bool isValid() const;
  static Member fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT MemberPage {
  QVector<Member> items;
  QString nextCursor;
  bool hasMore = false;
  QJsonObject raw;

  bool isValid() const;
  static MemberPage fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT Face {
  QString id;
  QString memberId;
  QString imageId;
  QString imageUrl;
  QString label;
  QString status;
  QString quality;
  double confidence = 0.0;
  QDateTime createdAt;
  QDateTime updatedAt;
  QJsonObject metadata;
  QJsonObject raw;

  bool isValid() const;
  static Face fromJson(const QJsonObject &json, const QString &memberId = {});
};

struct HUBSIGHT_ADMIN_EXPORT FacePage {
  QString memberId;
  QVector<Face> items;
  QString nextCursor;
  bool hasMore = false;
  QJsonObject raw;

  bool isValid() const;
  static FacePage fromJson(const QJsonObject &json,
                           const QString &memberId = {});
};

// Destructive member and face operations may return an empty 204 response.
// MemberClient marks those successful responses locally and preserves any
// server-provided action fields when present.
struct HUBSIGHT_ADMIN_EXPORT MemberActionResult {
  QString id;
  QString memberId;
  QString faceId;
  QString operationId;
  QString status;
  QString message;
  int affectedCount = 0;
  bool success = false;
  bool accepted = false;
  QJsonObject details;
  QJsonObject raw;

  bool isValid() const;
  static MemberActionResult fromJson(const QJsonObject &json,
                                     const QString &memberId = {},
                                     const QString &faceId = {});
};

struct HUBSIGHT_ADMIN_EXPORT MemberAvatar {
  QString memberId;
  QString id;
  QString imageId;
  QString url;
  QString mimeType;
  QString filename;
  QDateTime expiresAt;
  QJsonObject metadata;
  QJsonObject raw;

  bool isValid() const;
  static MemberAvatar fromJson(const QJsonObject &json,
                               const QString &memberId = {});
};

struct HUBSIGHT_ADMIN_EXPORT UploadedImage {
  QString id;
  QString uploadId;
  QString memberId;
  QString imageId;
  QString url;
  QString filename;
  QString mimeType;
  QString status;
  qint64 sizeBytes = 0;
  QJsonObject metadata;
  QJsonObject raw;

  bool isValid() const;
  static UploadedImage fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT ImageUploadResult {
  QString id;
  QString uploadId;
  QString status;
  QString message;
  QVector<UploadedImage> items;
  QJsonObject details;
  QJsonObject raw;

  bool isValid() const;
  static ImageUploadResult fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT PresignedImage {
  QString id;
  QString imageId;
  QString uploadId;
  QString url;
  QString method;
  QString filename;
  QString contentType;
  QDateTime expiresAt;
  QJsonObject headers;
  QJsonObject fields;
  QJsonObject raw;

  bool isValid() const;
  static PresignedImage fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT ImagePresignResult {
  QString uploadId;
  QString status;
  QString message;
  QVector<PresignedImage> items;
  QJsonObject details;
  QJsonObject raw;

  bool isValid() const;
  static ImagePresignResult fromJson(const QJsonObject &json);
};

// Resource-specific aliases keep the DTOs convenient for callers that use the
// endpoint vocabulary while retaining the canonical names used by signals.
using MemberOperationResult = MemberActionResult;
using MemberAvatarResult = MemberAvatar;
using ImageUpload = UploadedImage;
using ImagePresign = PresignedImage;

} // namespace HubSight::Admin

Q_DECLARE_METATYPE(HubSight::Admin::ImageUploadPart)
Q_DECLARE_METATYPE(HubSight::Admin::Member)
Q_DECLARE_METATYPE(HubSight::Admin::MemberPage)
Q_DECLARE_METATYPE(HubSight::Admin::Face)
Q_DECLARE_METATYPE(HubSight::Admin::FacePage)
Q_DECLARE_METATYPE(HubSight::Admin::MemberActionResult)
Q_DECLARE_METATYPE(HubSight::Admin::MemberAvatar)
Q_DECLARE_METATYPE(HubSight::Admin::UploadedImage)
Q_DECLARE_METATYPE(HubSight::Admin::ImageUploadResult)
Q_DECLARE_METATYPE(HubSight::Admin::PresignedImage)
Q_DECLARE_METATYPE(HubSight::Admin::ImagePresignResult)
