#include "../../include/hubsight/admin/resources/member_types.h"

#include <QJsonArray>
#include <QJsonValue>

#include <initializer_list>

namespace HubSight::Admin {
namespace {

QJsonObject objectPayload(const QJsonObject &json) {
  const QJsonValue data = json.value(QStringLiteral("data"));
  return data.isObject() ? data.toObject() : json;
}

QJsonObject nestedObject(const QJsonObject &json,
                         std::initializer_list<const char *> keys) {
  for (const char *key : keys) {
    const QJsonValue value = json.value(QString::fromLatin1(key));
    if (value.isObject()) {
      return value.toObject();
    }
  }
  return {};
}

QJsonObject resourceObject(const QJsonObject &json,
                           std::initializer_list<const char *> keys) {
  const QJsonObject source = objectPayload(json);
  const QJsonObject nested = nestedObject(source, keys);
  return nested.isEmpty() ? source : nested;
}

QString stringValue(const QJsonValue &value) {
  if (value.isString()) {
    return value.toString();
  }
  if (value.isDouble()) {
    return QString::number(value.toDouble(), 'g', 15);
  }
  return {};
}

QString firstString(const QJsonObject &json,
                    std::initializer_list<const char *> keys) {
  for (const char *key : keys) {
    const QString value = stringValue(json.value(QString::fromLatin1(key)));
    if (!value.isEmpty()) {
      return value;
    }
  }
  return {};
}

bool firstBool(const QJsonObject &json,
               std::initializer_list<const char *> keys, bool fallback) {
  for (const char *key : keys) {
    const QString name = QString::fromLatin1(key);
    if (json.contains(name)) {
      return json.value(name).toBool(fallback);
    }
  }
  return fallback;
}

qint64 integerValue(const QJsonObject &json,
                    std::initializer_list<const char *> keys) {
  for (const char *key : keys) {
    const QJsonValue value = json.value(QString::fromLatin1(key));
    if (value.isDouble()) {
      return static_cast<qint64>(value.toDouble());
    }
    if (value.isString()) {
      bool ok = false;
      const qint64 result = value.toString().toLongLong(&ok);
      if (ok) {
        return result;
      }
    }
  }
  return 0;
}

double doubleValue(const QJsonObject &json,
                   std::initializer_list<const char *> keys) {
  for (const char *key : keys) {
    const QJsonValue value = json.value(QString::fromLatin1(key));
    if (value.isDouble()) {
      return value.toDouble();
    }
    if (value.isString()) {
      bool ok = false;
      const double result = value.toString().toDouble(&ok);
      if (ok) {
        return result;
      }
    }
  }
  return 0.0;
}

QStringList stringList(const QJsonValue &value) {
  QStringList result;
  if (!value.isArray()) {
    return result;
  }
  for (const QJsonValue &item : value.toArray()) {
    if (item.isString() || item.isDouble()) {
      const QString text = stringValue(item);
      if (!text.isEmpty()) {
        result.append(text);
      }
    } else if (item.isObject()) {
      const QString text =
          firstString(item.toObject(), {"id", "name", "key", "value"});
      if (!text.isEmpty()) {
        result.append(text);
      }
    }
  }
  return result;
}

QStringList firstStringList(const QJsonObject &json,
                            std::initializer_list<const char *> keys) {
  for (const char *key : keys) {
    const QStringList result = stringList(json.value(QString::fromLatin1(key)));
    if (!result.isEmpty()) {
      return result;
    }
  }
  return {};
}

QJsonArray firstArray(const QJsonObject &json,
                      std::initializer_list<const char *> keys) {
  for (const char *key : keys) {
    const QJsonValue value = json.value(QString::fromLatin1(key));
    if (value.isArray()) {
      return value.toArray();
    }
  }
  return {};
}

QDateTime timestamp(const QJsonValue &value) {
  if (!value.isString()) {
    return {};
  }

  QString input = value.toString();
  const int dot = input.indexOf(QLatin1Char('.'));
  if (dot >= 0) {
    int fractionEnd = input.size();
    for (int index = dot + 1; index < input.size(); ++index) {
      const QChar character = input.at(index);
      if (character == QLatin1Char('Z') || character == QLatin1Char('+') ||
          character == QLatin1Char('-')) {
        fractionEnd = index;
        break;
      }
    }
    QString fraction = input.mid(dot + 1, fractionEnd - dot - 1).left(3);
    while (fraction.size() < 3) {
      fraction.append(QLatin1Char('0'));
    }
    input = input.left(dot + 1) + fraction + input.mid(fractionEnd);
  }

  QDateTime result = QDateTime::fromString(input, Qt::ISODateWithMs);
  if (!result.isValid()) {
    result = QDateTime::fromString(input, Qt::ISODate);
  }
  return result.isValid() ? result.toUTC() : QDateTime{};
}

QDateTime firstTimestamp(const QJsonObject &json,
                         std::initializer_list<const char *> keys) {
  for (const char *key : keys) {
    const QDateTime result = timestamp(json.value(QString::fromLatin1(key)));
    if (result.isValid()) {
      return result;
    }
  }
  return {};
}

QString pageNextCursor(const QJsonObject &json) {
  return firstString(json, {"next_cursor", "nextCursor", "cursor"});
}

bool pageHasMore(const QJsonObject &json, const QString &cursor) {
  if (json.contains(QStringLiteral("has_more"))) {
    return json.value(QStringLiteral("has_more")).toBool(!cursor.isEmpty());
  }
  if (json.contains(QStringLiteral("hasMore"))) {
    return json.value(QStringLiteral("hasMore")).toBool(!cursor.isEmpty());
  }
  return !cursor.isEmpty();
}

} // namespace

bool Member::isValid() const {
  return !id.trimmed().isEmpty() || !externalId.trimmed().isEmpty() ||
         !name.trimmed().isEmpty() || !displayName.trimmed().isEmpty() ||
         !raw.isEmpty();
}

Member Member::fromJson(const QJsonObject &json) {
  const QJsonObject source = resourceObject(json, {"member", "person"});
  Member result;
  result.id = firstString(source, {"id", "member_id", "memberId"});
  result.name = firstString(source, {"name", "full_name", "fullName"});
  result.displayName =
      firstString(source, {"display_name", "displayName", "name"});
  result.firstName = firstString(source, {"first_name", "firstName"});
  result.lastName = firstString(source, {"last_name", "lastName"});
  result.email =
      firstString(source, {"email", "email_address", "emailAddress"});
  result.phone = firstString(source, {"phone", "phone_number", "phoneNumber"});
  result.externalId =
      firstString(source, {"external_id", "externalId", "reference", "ref"});
  result.status = firstString(source, {"status", "state"});
  result.avatarUrl =
      firstString(source, {"avatar_url", "avatarUrl", "image_url", "imageUrl"});
  result.tags = firstStringList(source, {"tags", "labels"});
  result.faceCount = static_cast<int>(integerValue(
      source, {"face_count", "faceCount", "faces_count", "facesCount"}));
  result.active = firstBool(source, {"active", "is_active", "enabled"}, true);
  result.createdAt = firstTimestamp(source, {"created_at", "createdAt"});
  result.updatedAt = firstTimestamp(source, {"updated_at", "updatedAt"});
  result.metadata = nestedObject(source, {"metadata", "meta"});
  result.raw = source;
  return result;
}

bool MemberPage::isValid() const {
  for (const Member &item : items) {
    if (!item.isValid()) {
      return false;
    }
  }
  return !raw.isEmpty();
}

MemberPage MemberPage::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  MemberPage result;
  const QJsonArray values =
      firstArray(source, {"members", "items", "results", "data"});
  result.items.reserve(values.size());
  for (const QJsonValue &value : values) {
    if (value.isObject()) {
      result.items.append(Member::fromJson(value.toObject()));
    }
  }
  result.nextCursor = pageNextCursor(source);
  result.hasMore = pageHasMore(source, result.nextCursor);
  result.raw = source;
  return result;
}

bool Face::isValid() const {
  return !id.trimmed().isEmpty() || !imageId.trimmed().isEmpty() ||
         !imageUrl.trimmed().isEmpty() || !raw.isEmpty();
}

Face Face::fromJson(const QJsonObject &json, const QString &memberId) {
  const QJsonObject source = resourceObject(json, {"face", "member_face"});
  Face result;
  result.id = firstString(source, {"id", "face_id", "faceId"});
  result.memberId = memberId.trimmed().isEmpty()
                        ? firstString(source, {"member_id", "memberId"})
                        : memberId.trimmed();
  result.imageId =
      firstString(source, {"image_id", "imageId", "upload_id", "uploadId"});
  result.imageUrl = firstString(
      source, {"image_url", "imageUrl", "url", "face_url", "faceUrl"});
  result.label = firstString(source, {"label", "name"});
  result.status = firstString(source, {"status", "state"});
  result.quality = firstString(source, {"quality", "quality_score"});
  result.confidence = doubleValue(source, {"confidence", "score"});
  result.createdAt = firstTimestamp(source, {"created_at", "createdAt"});
  result.updatedAt = firstTimestamp(source, {"updated_at", "updatedAt"});
  result.metadata = nestedObject(source, {"metadata", "meta"});
  result.raw = source;
  return result;
}

bool FacePage::isValid() const {
  for (const Face &item : items) {
    if (!item.isValid()) {
      return false;
    }
  }
  return !raw.isEmpty();
}

FacePage FacePage::fromJson(const QJsonObject &json, const QString &memberId) {
  const QJsonObject source = objectPayload(json);
  FacePage result;
  result.memberId = memberId.trimmed().isEmpty()
                        ? firstString(source, {"member_id", "memberId"})
                        : memberId.trimmed();
  const QJsonArray values =
      firstArray(source, {"faces", "member_faces", "items", "results", "data"});
  result.items.reserve(values.size());
  for (const QJsonValue &value : values) {
    if (value.isObject()) {
      result.items.append(Face::fromJson(value.toObject(), result.memberId));
    }
  }
  result.nextCursor = pageNextCursor(source);
  result.hasMore = pageHasMore(source, result.nextCursor);
  result.raw = source;
  return result;
}

bool MemberActionResult::isValid() const {
  return success || accepted || !id.trimmed().isEmpty() ||
         !operationId.trimmed().isEmpty() || !status.trimmed().isEmpty() ||
         !message.trimmed().isEmpty() || !raw.isEmpty();
}

MemberActionResult MemberActionResult::fromJson(const QJsonObject &json,
                                                const QString &memberId,
                                                const QString &faceId) {
  const QJsonObject source = objectPayload(json);
  MemberActionResult result;
  result.id = firstString(source, {"id", "member_id", "face_id"});
  result.memberId = memberId.trimmed().isEmpty()
                        ? firstString(source, {"member_id", "memberId"})
                        : memberId.trimmed();
  result.faceId = faceId.trimmed().isEmpty()
                      ? firstString(source, {"face_id", "faceId"})
                      : faceId.trimmed();
  result.operationId =
      firstString(source, {"operation_id", "operationId", "job_id", "jobId"});
  result.status = firstString(source, {"status", "state", "result"});
  result.message = firstString(source, {"message", "message_en"});
  result.affectedCount = static_cast<int>(
      integerValue(source, {"affected", "affected_count", "affectedCount",
                            "deleted", "count"}));
  result.success = firstBool(source, {"success", "ok"}, false);
  result.accepted = firstBool(source, {"accepted", "queued"}, false);
  result.details = nestedObject(source, {"details", "result"});
  result.raw = source;
  return result;
}

bool MemberAvatar::isValid() const {
  return !memberId.trimmed().isEmpty() || !id.trimmed().isEmpty() ||
         !imageId.trimmed().isEmpty() || !url.trimmed().isEmpty() ||
         !raw.isEmpty();
}

MemberAvatar MemberAvatar::fromJson(const QJsonObject &json,
                                    const QString &memberId) {
  const QJsonObject source = resourceObject(json, {"avatar", "image"});
  MemberAvatar result;
  result.memberId = memberId.trimmed().isEmpty()
                        ? firstString(source, {"member_id", "memberId"})
                        : memberId.trimmed();
  result.id = firstString(source, {"id", "avatar_id", "avatarId"});
  result.imageId =
      firstString(source, {"image_id", "imageId", "upload_id", "uploadId"});
  result.url = firstString(source, {"url", "avatar_url", "avatarUrl",
                                    "image_url", "imageUrl", "location"});
  result.mimeType = firstString(
      source, {"mime_type", "mimeType", "content_type", "contentType", "type"});
  result.filename = firstString(source, {"filename", "file_name", "fileName"});
  result.expiresAt =
      firstTimestamp(source, {"expires_at", "expiresAt", "expiry"});
  result.metadata = nestedObject(source, {"metadata", "meta"});
  result.raw = source;
  return result;
}

bool UploadedImage::isValid() const {
  return !id.trimmed().isEmpty() || !uploadId.trimmed().isEmpty() ||
         !imageId.trimmed().isEmpty() || !url.trimmed().isEmpty() ||
         !raw.isEmpty();
}

UploadedImage UploadedImage::fromJson(const QJsonObject &json) {
  const QJsonObject source = resourceObject(json, {"image", "upload"});
  UploadedImage result;
  result.id = firstString(source, {"id", "image_id", "imageId"});
  result.uploadId = firstString(source, {"upload_id", "uploadId"});
  result.memberId = firstString(source, {"member_id", "memberId"});
  result.imageId = firstString(source, {"image_id", "imageId", "id"});
  result.url =
      firstString(source, {"url", "image_url", "imageUrl", "location"});
  result.filename = firstString(source, {"filename", "file_name", "fileName"});
  result.mimeType = firstString(
      source, {"mime_type", "mimeType", "content_type", "contentType", "type"});
  result.status = firstString(source, {"status", "state"});
  result.sizeBytes = integerValue(source, {"size_bytes", "sizeBytes", "size"});
  result.metadata = nestedObject(source, {"metadata", "meta"});
  result.raw = source;
  return result;
}

bool ImageUploadResult::isValid() const {
  for (const UploadedImage &item : items) {
    if (!item.isValid()) {
      return false;
    }
  }
  return !id.trimmed().isEmpty() || !uploadId.trimmed().isEmpty() ||
         !status.trimmed().isEmpty() || !message.trimmed().isEmpty() ||
         !items.isEmpty() || !raw.isEmpty();
}

ImageUploadResult ImageUploadResult::fromJson(const QJsonObject &json) {
  const QJsonObject source = resourceObject(json, {"upload", "result"});
  ImageUploadResult result;
  result.id = firstString(source, {"id", "upload_id", "uploadId"});
  result.uploadId = firstString(source, {"upload_id", "uploadId", "id"});
  result.status = firstString(source, {"status", "state"});
  result.message = firstString(source, {"message", "message_en"});
  const QJsonArray values =
      firstArray(source, {"images", "uploads", "items", "results", "data"});
  result.items.reserve(values.size());
  for (const QJsonValue &value : values) {
    if (value.isObject()) {
      result.items.append(UploadedImage::fromJson(value.toObject()));
    }
  }
  result.details = nestedObject(source, {"details", "metadata", "meta"});
  result.raw = source;
  return result;
}

bool PresignedImage::isValid() const {
  return !id.trimmed().isEmpty() || !imageId.trimmed().isEmpty() ||
         !uploadId.trimmed().isEmpty() || !url.trimmed().isEmpty() ||
         !raw.isEmpty();
}

PresignedImage PresignedImage::fromJson(const QJsonObject &json) {
  const QJsonObject source =
      resourceObject(json, {"image", "presign", "result"});
  PresignedImage result;
  result.id = firstString(source, {"id", "presign_id", "presignId"});
  result.imageId = firstString(source, {"image_id", "imageId"});
  result.uploadId = firstString(source, {"upload_id", "uploadId"});
  result.url =
      firstString(source, {"url", "upload_url", "uploadUrl", "presigned_url",
                           "presignedUrl", "location"});
  result.method = firstString(source, {"method", "http_method", "httpMethod"});
  result.filename = firstString(source, {"filename", "file_name", "fileName"});
  result.contentType = firstString(
      source, {"content_type", "contentType", "mime_type", "mimeType"});
  result.expiresAt =
      firstTimestamp(source, {"expires_at", "expiresAt", "expiry"});
  result.headers = nestedObject(source, {"headers", "request_headers"});
  result.fields = nestedObject(source, {"fields", "form_fields"});
  result.raw = source;
  return result;
}

bool ImagePresignResult::isValid() const {
  for (const PresignedImage &item : items) {
    if (!item.isValid()) {
      return false;
    }
  }
  return !uploadId.trimmed().isEmpty() || !status.trimmed().isEmpty() ||
         !message.trimmed().isEmpty() || !items.isEmpty() || !raw.isEmpty();
}

ImagePresignResult ImagePresignResult::fromJson(const QJsonObject &json) {
  const QJsonObject source = resourceObject(json, {"presign", "result"});
  ImagePresignResult result;
  result.uploadId = firstString(source, {"upload_id", "uploadId", "id"});
  result.status = firstString(source, {"status", "state"});
  result.message = firstString(source, {"message", "message_en"});
  const QJsonArray values =
      firstArray(source, {"images", "presigned", "presigned_images", "items",
                          "results", "data"});
  result.items.reserve(values.size());
  for (const QJsonValue &value : values) {
    if (value.isObject()) {
      result.items.append(PresignedImage::fromJson(value.toObject()));
    }
  }
  result.details = nestedObject(source, {"details", "metadata", "meta"});
  result.raw = source;
  return result;
}

} // namespace HubSight::Admin
