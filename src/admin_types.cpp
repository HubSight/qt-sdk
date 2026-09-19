#include "../include/hubsight/admin/admin_types.h"

#include <QJsonArray>
#include <QMetaType>

namespace HubSight::Admin {
namespace {

QDateTime parseTimestamp(const QJsonValue &value) {
  if (!value.isString()) {
    return {};
  }

  // Qt stores millisecond precision while Go's RFC3339Nano may emit a longer
  // fractional part. Truncate only the excess precision before parsing.
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
  return result.toUTC();
}

QStringList stringList(const QJsonValue &value) {
  QStringList result;
  if (!value.isArray()) {
    return result;
  }
  for (const QJsonValue &item : value.toArray()) {
    if (item.isString()) {
      result.append(item.toString());
    }
  }
  return result;
}

QJsonObject nestedData(const QJsonObject &json) {
  const QJsonValue data = json.value(QStringLiteral("data"));
  return data.isObject() ? data.toObject() : json;
}

} // namespace

bool AdminError::isMaintenance() const {
  return category == ErrorCategory::Maintenance ||
         serverCode == QStringLiteral("ADMIN_API_DISABLED");
}

bool AdminError::isAuthenticationError() const {
  return category == ErrorCategory::Authentication || httpStatus == 401;
}

bool AdminError::isNetworkError() const {
  return category == ErrorCategory::Network ||
         category == ErrorCategory::Canceled;
}

bool TokenSet::isValid() const { return !accessToken.trimmed().isEmpty(); }

TokenSet TokenSet::fromJson(const QJsonObject &json) {
  const QJsonObject source = nestedData(json);
  TokenSet result;
  result.accessToken = source.value(QStringLiteral("access_token")).toString();
  if (result.accessToken.isEmpty()) {
    result.accessToken = source.value(QStringLiteral("token")).toString();
  }
  result.refreshToken =
      source.value(QStringLiteral("refresh_token")).toString();
  result.tokenType = source.value(QStringLiteral("token_type"))
                         .toString(QStringLiteral("Bearer"));
  result.expiresInSeconds = source.value(QStringLiteral("expires_in")).toInt();
  result.clientId = source.value(QStringLiteral("client_id")).toString();
  return result;
}

bool AdminUser::isValid() const {
  return !id.trimmed().isEmpty() || !username.trimmed().isEmpty();
}

AdminUser AdminUser::fromJson(const QJsonObject &json) {
  const QJsonObject source = nestedData(json);
  AdminUser result;
  result.id = source.value(QStringLiteral("id")).toString();
  result.username = source.value(QStringLiteral("username")).toString();
  result.fullName = source.value(QStringLiteral("full_name")).toString();
  result.role = source.value(QStringLiteral("role")).toString();
  result.locale = source.value(QStringLiteral("locale")).toString();
  result.timezone = source.value(QStringLiteral("timezone")).toString();
  result.permissions = stringList(source.value(QStringLiteral("permissions")));
  if (source.contains(QStringLiteral("is_active"))) {
    result.active = source.value(QStringLiteral("is_active")).toBool(true);
  } else if (source.contains(QStringLiteral("active"))) {
    result.active = source.value(QStringLiteral("active")).toBool(true);
  }
  return result;
}

SystemStatus SystemStatus::fromJson(const QJsonObject &json) {
  SystemStatus result;
  result.apiVersion = json.value(QStringLiteral("api_version")).toString();
  if (result.apiVersion.isEmpty()) {
    result.apiVersion =
        json.value(QStringLiteral("admin_api_version")).toString();
  }
  result.adminApiEnabled =
      json.value(QStringLiteral("admin_api_enabled")).toBool(false);
  result.serverTime = parseTimestamp(json.value(QStringLiteral("server_time")));
  result.features = json.value(QStringLiteral("features")).toObject();
  result.requestId = json.value(QStringLiteral("request_id")).toString();
  return result;
}

Capabilities Capabilities::fromJson(const QJsonObject &json) {
  const QJsonObject source = nestedData(json);
  Capabilities result;
  result.matrixLimit = source.value(QStringLiteral("matrix_limit")).toInt();
  result.supportedMatrixProfiles =
      stringList(source.value(QStringLiteral("supported_matrix_profiles")));
  result.realtimeTransport =
      source.value(QStringLiteral("realtime_transport")).toString();
  result.authentication =
      stringList(source.value(QStringLiteral("authentication")));
  result.mediaTransport =
      source.value(QStringLiteral("media_transport")).toString();
  result.requestId = json.value(QStringLiteral("request_id")).toString();
  return result;
}

bool CameraSummary::isValid() const { return !id.trimmed().isEmpty(); }

CameraSummary CameraSummary::fromJson(const QJsonObject &json) {
  CameraSummary result;
  result.id = json.value(QStringLiteral("id")).toString();
  result.name = json.value(QStringLiteral("name")).toString();
  result.host = json.value(QStringLiteral("host")).toString();
  result.brand = json.value(QStringLiteral("brand")).toString();
  result.rtspPort = json.value(QStringLiteral("rtsp_port")).toInt();
  result.rtspTransport =
      json.value(QStringLiteral("rtsp_transport")).toString();
  result.active = json.value(QStringLiteral("is_active")).toBool(false);
  result.stopped = json.value(QStringLiteral("is_stopped")).toBool(false);
  result.aiEnabled = json.value(QStringLiteral("enable_ai")).toBool(false);
  result.showBbox = json.value(QStringLiteral("show_bbox")).toBool(false);
  result.nvrMode = json.value(QStringLiteral("nvr_mode")).toString();
  result.recordQuality =
      json.value(QStringLiteral("record_quality")).toString();
  result.fixed = json.value(QStringLiteral("is_fixed")).toBool(false);
  result.homographyValid =
      json.value(QStringLiteral("homography_valid")).toBool(false);
  result.onvifEnabled =
      json.value(QStringLiteral("onvif_enabled")).toBool(false);
  result.ptzSupported =
      json.value(QStringLiteral("onvif_ptz_supported")).toBool(false);
  result.createdAt = parseTimestamp(json.value(QStringLiteral("created_at")));
  result.updatedAt = parseTimestamp(json.value(QStringLiteral("updated_at")));
  return result;
}

} // namespace HubSight::Admin
