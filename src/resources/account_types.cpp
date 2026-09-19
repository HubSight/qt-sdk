#include "../../include/hubsight/admin/resources/account_types.h"

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

QString firstString(const QJsonObject &json,
                    std::initializer_list<const char *> keys) {
  for (const char *key : keys) {
    const QString value = json.value(QString::fromLatin1(key)).toString();
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

int firstInt(const QJsonObject &json, std::initializer_list<const char *> keys,
             int fallback = 0) {
  for (const char *key : keys) {
    const QJsonValue value = json.value(QString::fromLatin1(key));
    if (value.isDouble()) {
      return static_cast<int>(value.toDouble());
    }
    if (value.isString()) {
      bool ok = false;
      const int result = value.toString().toInt(&ok);
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
    if (item.isString()) {
      result.append(item.toString());
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

QJsonObject passkeyObject(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  const QJsonObject result = nestedObject(source, {"passkey", "credential"});
  return result.isEmpty() ? source : result;
}

} // namespace

bool PasswordVerification::isValid() const {
  return !raw.isEmpty() || verified || !status.isEmpty();
}

PasswordVerification PasswordVerification::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  PasswordVerification result;
  result.verified = firstBool(source, {"verified", "valid", "success"}, false);
  result.status = firstString(source, {"status", "result"});
  result.message = firstString(source, {"message", "message_en"});
  result.raw = source;
  return result;
}

bool AccountActionResult::isValid() const {
  return success || !status.isEmpty() || !message.isEmpty() ||
         affectedCount != 0 || !raw.isEmpty();
}

AccountActionResult AccountActionResult::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  AccountActionResult result;
  result.success = firstBool(source, {"success", "ok"}, false);
  result.status = firstString(source, {"status", "result"});
  result.message = firstString(source, {"message", "message_en"});
  result.affectedCount = firstInt(
      source, {"affected", "affected_count", "affectedCount", "count"});
  result.raw = source;
  return result;
}

bool AccountProfile::isValid() const {
  return !id.trimmed().isEmpty() || !username.trimmed().isEmpty() ||
         !email.trimmed().isEmpty();
}

AccountProfile AccountProfile::fromJson(const QJsonObject &json) {
  QJsonObject source = objectPayload(json);
  const QJsonObject profile = nestedObject(source, {"profile", "user"});
  if (!profile.isEmpty()) {
    source = profile;
  }

  AccountProfile result;
  result.id = firstString(source, {"id", "user_id", "userId"});
  result.username = firstString(source, {"username", "login"});
  result.email =
      firstString(source, {"email", "email_address", "emailAddress"});
  result.fullName = firstString(source, {"full_name", "fullName", "name"});
  result.role = source.value(QStringLiteral("role")).toString();
  result.locale = source.value(QStringLiteral("locale")).toString();
  result.timezone = source.value(QStringLiteral("timezone")).toString();
  result.avatarUrl = firstString(source, {"avatar_url", "avatarUrl"});
  result.permissions = firstStringList(source, {"permissions"});
  result.active = firstBool(source, {"is_active", "active"}, true);
  result.raw = source;
  return result;
}

bool AccountSession::isValid() const {
  return !id.trimmed().isEmpty() || !sessionId.trimmed().isEmpty();
}

AccountSession AccountSession::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  AccountSession result;
  result.id = firstString(source, {"id", "session_id", "sessionId"});
  result.sessionId = firstString(source, {"session_id", "sessionId", "id"});
  result.deviceName =
      firstString(source, {"device_name", "deviceName", "name"});
  result.platform = source.value(QStringLiteral("platform")).toString();
  result.ipAddress = firstString(source, {"ip_address", "ipAddress", "ip"});
  result.userAgent = firstString(source, {"user_agent", "userAgent"});
  result.createdAt = firstTimestamp(source, {"created_at", "createdAt"});
  result.lastUsedAt = firstTimestamp(
      source, {"last_used_at", "lastUsedAt", "last_active_at", "lastActiveAt"});
  result.expiresAt = firstTimestamp(source, {"expires_at", "expiresAt"});
  result.current = firstBool(source, {"current", "is_current"}, false);
  result.revoked = firstBool(source, {"revoked", "is_revoked"}, false);
  result.raw = source;
  return result;
}

bool AccountSessionPage::isValid() const {
  for (const AccountSession &item : items) {
    if (!item.isValid()) {
      return false;
    }
  }
  return !raw.isEmpty();
}

AccountSessionPage AccountSessionPage::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  AccountSessionPage result;
  const QJsonArray values = firstArray(source, {"sessions", "items", "data"});
  result.items.reserve(values.size());
  for (const QJsonValue &value : values) {
    if (value.isObject()) {
      result.items.append(AccountSession::fromJson(value.toObject()));
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

bool TwoFactorSetup::isValid() const {
  return !secret.isEmpty() || !otpAuthUri.isEmpty() || !qrCode.isEmpty() ||
         !raw.isEmpty();
}

TwoFactorSetup TwoFactorSetup::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  TwoFactorSetup result;
  result.enabled = firstBool(source, {"enabled", "is_enabled"}, false);
  result.secret = firstString(source, {"secret", "totp_secret"});
  result.otpAuthUri =
      firstString(source, {"otpauth_uri", "otp_auth_uri", "otpAuthUri", "uri"});
  result.qrCode = firstString(source, {"qr_code", "qrCode", "qr"});
  result.issuer = source.value(QStringLiteral("issuer")).toString();
  result.account = source.value(QStringLiteral("account")).toString();
  result.recoveryCodes =
      firstStringList(source, {"recovery_codes", "recoveryCodes", "codes"});
  result.raw = source;
  return result;
}

bool RecoveryCodes::isValid() const {
  return !codes.isEmpty() || !raw.isEmpty();
}

RecoveryCodes RecoveryCodes::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  RecoveryCodes result;
  result.codes =
      firstStringList(source, {"recovery_codes", "recoveryCodes", "codes"});
  result.count = firstInt(source, {"count", "code_count", "codeCount"},
                          result.codes.size());
  result.raw = source;
  return result;
}

bool Passkey::isValid() const {
  return !id.trimmed().isEmpty() || !credentialId.trimmed().isEmpty();
}

Passkey Passkey::fromJson(const QJsonObject &json) {
  const QJsonObject source = passkeyObject(json);
  Passkey result;
  result.id = firstString(source, {"id", "passkey_id", "passkeyId"});
  result.name = firstString(source, {"name", "display_name", "displayName"});
  result.credentialId =
      firstString(source, {"credential_id", "credentialId", "raw_id", "rawId"});
  result.transports = firstStringList(source, {"transports"});
  result.createdAt = firstTimestamp(source, {"created_at", "createdAt"});
  result.lastUsedAt =
      firstTimestamp(source, {"last_used_at", "lastUsedAt", "last_used"});
  result.backedUp = firstBool(source, {"backed_up", "backedUp"}, false);
  result.userVerified =
      firstBool(source, {"user_verified", "userVerified"}, false);
  result.raw = source;
  return result;
}

bool PasskeyPage::isValid() const {
  for (const Passkey &item : items) {
    if (!item.isValid()) {
      return false;
    }
  }
  return !raw.isEmpty();
}

PasskeyPage PasskeyPage::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  PasskeyPage result;
  const QJsonArray values = firstArray(source, {"passkeys", "items", "data"});
  result.items.reserve(values.size());
  for (const QJsonValue &value : values) {
    if (value.isObject()) {
      result.items.append(Passkey::fromJson(value.toObject()));
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

bool PasskeyOptions::isValid() const {
  return !challenge.trimmed().isEmpty() || !publicKey.isEmpty() ||
         !raw.isEmpty();
}

PasskeyOptions PasskeyOptions::fromJson(const QJsonObject &json) {
  QJsonObject source = objectPayload(json);
  QJsonObject publicKey = nestedObject(source, {"public_key", "publicKey"});
  if (publicKey.isEmpty()) {
    publicKey = source;
  }

  PasskeyOptions result;
  result.publicKey = publicKey;
  result.challenge = firstString(publicKey, {"challenge"});
  result.timeout = firstInt(publicKey, {"timeout"});
  result.user = nestedObject(publicKey, {"user"});
  result.relyingParty =
      nestedObject(publicKey, {"rp", "relying_party", "relyingParty"});
  result.relyingPartyId =
      firstString(result.relyingParty, {"id", "rp_id", "rpId"});
  result.userId = firstString(result.user, {"id", "user_id", "userId"});
  result.allowCredentials =
      firstArray(publicKey, {"allowCredentials", "allow_credentials"});
  result.excludeCredentials =
      firstArray(publicKey, {"excludeCredentials", "exclude_credentials"});
  result.raw = source;
  return result;
}

bool PasskeyOperationResult::isValid() const {
  return success || passkey.isValid() || action.isValid() || !raw.isEmpty();
}

PasskeyOperationResult
PasskeyOperationResult::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  PasskeyOperationResult result;
  result.passkey = Passkey::fromJson(source);
  result.action = AccountActionResult::fromJson(source);
  result.success = firstBool(source, {"success", "ok"}, false);
  result.status = firstString(source, {"status", "result"});
  result.raw = source;
  return result;
}

bool PasskeyLoginResult::isValid() const {
  return tokens.isValid() || user.isValid() || authenticated || !raw.isEmpty();
}

PasskeyLoginResult PasskeyLoginResult::fromJson(const QJsonObject &json) {
  const QJsonObject source = objectPayload(json);
  PasskeyLoginResult result;
  QJsonObject tokenObject = nestedObject(source, {"tokens", "token_set"});
  if (tokenObject.isEmpty()) {
    tokenObject = source;
  }
  result.tokens = TokenSet::fromJson(tokenObject);
  const QJsonObject userObject = nestedObject(source, {"user", "profile"});
  result.user = AdminUser::fromJson(userObject.isEmpty() ? source : userObject);
  result.authenticated = firstBool(source, {"authenticated", "success", "ok"},
                                   result.tokens.isValid());
  result.status = firstString(source, {"status", "result"});
  result.raw = source;
  return result;
}

} // namespace HubSight::Admin
