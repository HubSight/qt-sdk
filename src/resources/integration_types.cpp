#include "../../include/hubsight/admin/resources/integration_types.h"

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

double firstDouble(const QJsonObject &json,
                   std::initializer_list<const char *> keys,
                   double fallback = 0.0) {
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
  return fallback;
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
          firstString(item.toObject(), {"id", "key", "name", "scope"});
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

} // namespace

bool ApiClient::isValid() const {
  return !id.trimmed().isEmpty() || !clientId.trimmed().isEmpty() ||
         !name.trimmed().isEmpty() || !raw.isEmpty();
}

ApiClient ApiClient::fromJson(const QJsonObject &json) {
  const QJsonObject source = resourceObject(json, {"client", "api_client"});
  ApiClient result;
  result.id = firstString(source, {"id", "client_id", "clientId"});
  result.clientId = firstString(source, {"client_id", "clientId", "id"});
  result.name = firstString(source, {"name", "display_name", "displayName"});
  result.description = firstString(source, {"description", "description_en"});
  result.type = firstString(source, {"type", "client_type", "clientType"});
  result.status = firstString(source, {"status", "state"});
  result.key = firstString(source, {"key", "api_key", "apiKey"});
  result.secret =
      firstString(source, {"secret", "client_secret", "clientSecret"});
  result.clientSecret = firstString(source, {"client_secret", "clientSecret"});
  result.scopes = firstStringList(source, {"scopes", "permissions"});
  result.enabled = firstBool(source, {"enabled", "is_enabled"}, true);
  result.active = firstBool(source, {"active", "is_active"}, result.enabled);
  result.verified = firstBool(source, {"verified", "is_verified"}, false);
  result.createdAt = firstTimestamp(source, {"created_at", "createdAt"});
  result.updatedAt = firstTimestamp(source, {"updated_at", "updatedAt"});
  result.lastUsedAt =
      firstTimestamp(source, {"last_used_at", "lastUsedAt", "last_used"});
  result.expiresAt = firstTimestamp(source, {"expires_at", "expiresAt"});
  result.raw = source;
  return result;
}

bool ApiClientPage::isValid() const {
  for (const ApiClient &item : items) {
    if (!item.isValid()) {
      return false;
    }
  }
  return !raw.isEmpty();
}

ApiClientPage ApiClientPage::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  ApiClientPage result;
  const QJsonArray values =
      firstArray(source, {"clients", "api_clients", "items", "data"});
  result.items.reserve(values.size());
  for (const QJsonValue &value : values) {
    if (value.isObject()) {
      result.items.append(ApiClient::fromJson(value.toObject()));
    }
  }
  result.nextCursor =
      firstString(source, {"next_cursor", "nextCursor", "cursor"});
  result.hasMore = source.contains(QStringLiteral("has_more"))
                       ? source.value(QStringLiteral("has_more")).toBool()
                       : source.value(QStringLiteral("hasMore"))
                             .toBool(!result.nextCursor.isEmpty());
  result.raw = source;
  return result;
}

bool GoogleServiceAccount::isValid() const {
  return !id.trimmed().isEmpty() || !accountId.trimmed().isEmpty() ||
         !email.trimmed().isEmpty() || !clientEmail.trimmed().isEmpty() ||
         !raw.isEmpty();
}

GoogleServiceAccount GoogleServiceAccount::fromJson(const QJsonObject &json) {
  const QJsonObject source = resourceObject(
      json, {"account", "service_account", "google_service_account"});
  GoogleServiceAccount result;
  result.id = firstString(source, {"id", "account_id", "accountId"});
  result.accountId = firstString(source, {"account_id", "accountId", "id"});
  result.name = firstString(source, {"name", "display_name", "displayName"});
  result.email =
      firstString(source, {"email", "email_address", "emailAddress"});
  result.clientEmail = firstString(
      source, {"client_email", "clientEmail", "service_account_email"});
  result.projectId = firstString(source, {"project_id", "projectId"});
  result.projectNumber =
      firstString(source, {"project_number", "projectNumber"});
  result.privateKeyId = firstString(source, {"private_key_id", "privateKeyId"});
  result.type = firstString(source, {"type", "account_type", "accountType"});
  result.status = firstString(source, {"status", "state"});
  result.active = firstBool(source, {"active", "is_active"}, false);
  result.enabled = firstBool(source, {"enabled", "is_enabled"}, true);
  result.firebaseReady = firstBool(
      source, {"firebase_ready", "firebaseReady", "firebase_enabled"}, false);
  result.createdAt = firstTimestamp(source, {"created_at", "createdAt"});
  result.updatedAt = firstTimestamp(source, {"updated_at", "updatedAt"});
  result.credentials = nestedObject(source, {"credentials", "service_account"});
  result.raw = source;
  return result;
}

bool GoogleServiceAccountPage::isValid() const {
  for (const GoogleServiceAccount &item : items) {
    if (!item.isValid()) {
      return false;
    }
  }
  return !raw.isEmpty();
}

GoogleServiceAccountPage
GoogleServiceAccountPage::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  GoogleServiceAccountPage result;
  const QJsonArray values =
      firstArray(source, {"accounts", "service_accounts", "items", "data"});
  result.items.reserve(values.size());
  for (const QJsonValue &value : values) {
    if (value.isObject()) {
      result.items.append(GoogleServiceAccount::fromJson(value.toObject()));
    }
  }
  result.nextCursor =
      firstString(source, {"next_cursor", "nextCursor", "cursor"});
  result.hasMore = source.contains(QStringLiteral("has_more"))
                       ? source.value(QStringLiteral("has_more")).toBool()
                       : source.value(QStringLiteral("hasMore"))
                             .toBool(!result.nextCursor.isEmpty());
  result.raw = source;
  return result;
}

bool AppConfig::isValid() const {
  return !id.trimmed().isEmpty() || !configId.trimmed().isEmpty() ||
         !name.trimmed().isEmpty() || !raw.isEmpty();
}

AppConfig AppConfig::fromJson(const QJsonObject &json) {
  const QJsonObject source = resourceObject(json, {"config", "app_config"});
  AppConfig result;
  result.id = firstString(source, {"id", "config_id", "configId"});
  result.configId = firstString(source, {"config_id", "configId", "id"});
  result.name = firstString(source, {"name", "display_name", "displayName"});
  result.platform = firstString(source, {"platform", "target_platform"});
  result.applicationId = firstString(
      source, {"application_id", "applicationId", "app_id", "appId"});
  result.packageName = firstString(source, {"package_name", "packageName"});
  result.bundleId = firstString(source, {"bundle_id", "bundleId"});
  result.clientId = firstString(source, {"client_id", "clientId"});
  result.status = firstString(source, {"status", "state"});
  result.downloadUrl = firstString(source, {"download_url", "downloadUrl"});
  result.qrCode = firstString(source, {"qr_code", "qrCode"});
  result.active = firstBool(source, {"active", "is_active", "enabled"}, true);
  result.revoked = firstBool(source, {"revoked", "is_revoked"}, false);
  result.createdAt = firstTimestamp(source, {"created_at", "createdAt"});
  result.updatedAt = firstTimestamp(source, {"updated_at", "updatedAt"});
  result.expiresAt = firstTimestamp(source, {"expires_at", "expiresAt"});
  result.raw = source;
  return result;
}

bool AppConfigPage::isValid() const {
  for (const AppConfig &item : items) {
    if (!item.isValid()) {
      return false;
    }
  }
  return !raw.isEmpty();
}

AppConfigPage AppConfigPage::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  AppConfigPage result;
  const QJsonArray values =
      firstArray(source, {"configs", "app_configs", "items", "data"});
  result.items.reserve(values.size());
  for (const QJsonValue &value : values) {
    if (value.isObject()) {
      result.items.append(AppConfig::fromJson(value.toObject()));
    }
  }
  result.nextCursor =
      firstString(source, {"next_cursor", "nextCursor", "cursor"});
  result.hasMore = source.contains(QStringLiteral("has_more"))
                       ? source.value(QStringLiteral("has_more")).toBool()
                       : source.value(QStringLiteral("hasMore"))
                             .toBool(!result.nextCursor.isEmpty());
  result.raw = source;
  return result;
}

bool IntegrationActionResult::isValid() const {
  return success || accepted || !status.isEmpty() || !message.isEmpty() ||
         !id.isEmpty() || !operationId.isEmpty() || !raw.isEmpty();
}

IntegrationActionResult
IntegrationActionResult::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  IntegrationActionResult result;
  result.id =
      firstString(source, {"id", "client_id", "account_id", "config_id"});
  result.resourceId = firstString(source, {"resource_id", "resourceId"});
  result.operationId =
      firstString(source, {"operation_id", "operationId", "job_id", "jobId"});
  result.status = firstString(source, {"status", "result"});
  result.message = firstString(source, {"message", "message_en"});
  result.success = firstBool(source, {"success", "ok"}, false);
  result.accepted = firstBool(source, {"accepted", "queued"}, false);
  result.details = nestedObject(source, {"details", "result"});
  result.raw = source;
  return result;
}

bool IntegrationUrlResult::isValid() const {
  return !url.trimmed().isEmpty() || !resourceId.trimmed().isEmpty() ||
         !raw.isEmpty();
}

IntegrationUrlResult IntegrationUrlResult::fromJson(const QJsonObject &json,
                                                    const QString &resourceId) {
  const QJsonObject source = resourceObject(json, {"download", "result"});
  IntegrationUrlResult result;
  result.id = firstString(source, {"id", "download_id"});
  result.resourceId = resourceId;
  if (result.resourceId.isEmpty()) {
    result.resourceId =
        firstString(source, {"resource_id", "config_id", "configId"});
  }
  result.url = firstString(source, {"url", "download_url", "downloadUrl"});
  result.filename = firstString(source, {"filename", "file_name", "fileName"});
  result.contentType =
      firstString(source, {"content_type", "contentType", "mime_type"});
  result.expiresAt = firstTimestamp(source, {"expires_at", "expiresAt"});
  result.raw = source;
  return result;
}

bool IntegrationQrResult::isValid() const {
  return !qrCode.trimmed().isEmpty() || !data.trimmed().isEmpty() ||
         !resourceId.trimmed().isEmpty() || !raw.isEmpty();
}

IntegrationQrResult IntegrationQrResult::fromJson(const QJsonObject &json,
                                                  const QString &resourceId) {
  const QJsonObject source = resourceObject(json, {"qr", "qr_code", "result"});
  IntegrationQrResult result;
  result.id = firstString(source, {"id", "qr_id"});
  result.resourceId = resourceId;
  if (result.resourceId.isEmpty()) {
    result.resourceId =
        firstString(source, {"resource_id", "config_id", "configId"});
  }
  result.qrCode = firstString(source, {"qr_code", "qrCode", "code"});
  result.data = firstString(source, {"data", "content", "value"});
  result.format = firstString(source, {"format", "encoding", "type"});
  result.raw = source;
  return result;
}

bool FirebasePreflightResult::isValid() const {
  return ready || enabled || !status.isEmpty() || !message.isEmpty() ||
         !accountId.trimmed().isEmpty() || !raw.isEmpty();
}

FirebasePreflightResult
FirebasePreflightResult::fromJson(const QJsonObject &json,
                                  const QString &accountId) {
  const QJsonObject source = resourceObject(json, {"preflight", "firebase"});
  FirebasePreflightResult result;
  result.accountId = accountId;
  if (result.accountId.isEmpty()) {
    result.accountId = firstString(source, {"account_id", "accountId", "id"});
  }
  result.status = firstString(source, {"status", "state"});
  result.message = firstString(source, {"message", "message_en"});
  result.missing =
      firstStringList(source, {"missing", "missing_fields", "errors"});
  result.ready = firstBool(source, {"ready", "is_ready", "ok"}, false);
  result.enabled = firstBool(source, {"enabled", "firebase_enabled"}, false);
  result.details = nestedObject(source, {"details", "checks", "result"});
  result.raw = source;
  return result;
}

bool IntegrationOperation::isValid() const {
  return !id.trimmed().isEmpty() || !operationId.trimmed().isEmpty() ||
         !status.trimmed().isEmpty() || !raw.isEmpty();
}

IntegrationOperation IntegrationOperation::fromJson(const QJsonObject &json) {
  const QJsonObject source = resourceObject(json, {"operation"});
  IntegrationOperation result;
  result.id = firstString(source, {"id", "operation_id", "operationId"});
  result.operationId =
      firstString(source, {"operation_id", "operationId", "id"});
  result.name =
      firstString(source, {"name", "operation_name", "operationName"});
  result.type =
      firstString(source, {"type", "operation_type", "operationType"});
  result.status = firstString(source, {"status", "state"});
  result.message = firstString(source, {"message", "message_en"});
  result.progress = firstDouble(source, {"progress", "progress_percent"});
  result.done = firstBool(source, {"done", "completed", "is_done"}, false);
  result.successful = firstBool(source, {"successful", "success", "ok"}, false);
  result.cancelable =
      firstBool(source, {"cancelable", "cancellable", "can_cancel"}, false);
  result.createdAt = firstTimestamp(source, {"created_at", "createdAt"});
  result.updatedAt = firstTimestamp(source, {"updated_at", "updatedAt"});
  result.completedAt =
      firstTimestamp(source, {"completed_at", "completedAt", "finished_at"});
  result.response = nestedObject(source, {"response", "result", "output"});
  result.error = nestedObject(source, {"error", "failure"});
  result.metadata = nestedObject(source, {"metadata", "meta"});
  result.raw = source;
  return result;
}

} // namespace HubSight::Admin
