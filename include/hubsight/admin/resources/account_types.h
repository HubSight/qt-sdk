#pragma once

#include "../admin_export.h"
#include "../admin_types.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace HubSight::Admin {

struct HUBSIGHT_ADMIN_EXPORT PasswordVerification {
  bool verified = false;
  QString status;
  QString message;
  QJsonObject raw;

  bool isValid() const;
  static PasswordVerification fromJson(const QJsonObject &json);
};

// Common response for account mutations. A successful endpoint may return an
// empty 204 response; AccountClient marks that response successful locally.
struct HUBSIGHT_ADMIN_EXPORT AccountActionResult {
  bool success = false;
  QString status;
  QString message;
  int affectedCount = 0;
  QJsonObject raw;

  bool isValid() const;
  static AccountActionResult fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT AccountProfile {
  QString id;
  QString username;
  QString email;
  QString fullName;
  QString role;
  QString locale;
  QString timezone;
  QString avatarUrl;
  QStringList permissions;
  bool active = true;
  QJsonObject raw;

  bool isValid() const;
  static AccountProfile fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT AccountSession {
  QString id;
  QString sessionId;
  QString deviceName;
  QString platform;
  QString ipAddress;
  QString userAgent;
  QDateTime createdAt;
  QDateTime lastUsedAt;
  QDateTime expiresAt;
  bool current = false;
  bool revoked = false;
  QJsonObject raw;

  bool isValid() const;
  static AccountSession fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT AccountSessionPage {
  QVector<AccountSession> items;
  QString nextCursor;
  bool hasMore = false;
  QJsonObject raw;

  bool isValid() const;
  static AccountSessionPage fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT TwoFactorSetup {
  bool enabled = false;
  QString secret;
  QString otpAuthUri;
  QString qrCode;
  QString issuer;
  QString account;
  QStringList recoveryCodes;
  QJsonObject raw;

  bool isValid() const;
  static TwoFactorSetup fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT RecoveryCodes {
  QStringList codes;
  int count = 0;
  QJsonObject raw;

  bool isValid() const;
  static RecoveryCodes fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT Passkey {
  QString id;
  QString name;
  QString credentialId;
  QStringList transports;
  QDateTime createdAt;
  QDateTime lastUsedAt;
  bool backedUp = false;
  bool userVerified = false;
  QJsonObject raw;

  bool isValid() const;
  static Passkey fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT PasskeyPage {
  QVector<Passkey> items;
  QString nextCursor;
  bool hasMore = false;
  QJsonObject raw;

  bool isValid() const;
  static PasskeyPage fromJson(const QJsonObject &json);
};

// The WebAuthn option objects are intentionally preserved in addition to the
// common fields. This keeps extensions such as authenticatorSelection and
// extensions available without making the SDK guess at browser API details.
struct HUBSIGHT_ADMIN_EXPORT PasskeyOptions {
  QString challenge;
  QString relyingPartyId;
  QString userId;
  int timeout = 0;
  QJsonObject publicKey;
  QJsonObject user;
  QJsonObject relyingParty;
  QJsonArray allowCredentials;
  QJsonArray excludeCredentials;
  QJsonObject raw;

  bool isValid() const;
  static PasskeyOptions fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT PasskeyOperationResult {
  Passkey passkey;
  AccountActionResult action;
  bool success = false;
  QString status;
  QJsonObject raw;

  bool isValid() const;
  static PasskeyOperationResult fromJson(const QJsonObject &json);
};

struct HUBSIGHT_ADMIN_EXPORT PasskeyLoginResult {
  TokenSet tokens;
  AdminUser user;
  bool authenticated = false;
  QString status;
  QJsonObject raw;

  bool isValid() const;
  static PasskeyLoginResult fromJson(const QJsonObject &json);
};

} // namespace HubSight::Admin

Q_DECLARE_METATYPE(HubSight::Admin::PasswordVerification)
Q_DECLARE_METATYPE(HubSight::Admin::AccountActionResult)
Q_DECLARE_METATYPE(HubSight::Admin::AccountProfile)
Q_DECLARE_METATYPE(HubSight::Admin::AccountSession)
Q_DECLARE_METATYPE(HubSight::Admin::AccountSessionPage)
Q_DECLARE_METATYPE(HubSight::Admin::TwoFactorSetup)
Q_DECLARE_METATYPE(HubSight::Admin::RecoveryCodes)
Q_DECLARE_METATYPE(HubSight::Admin::Passkey)
Q_DECLARE_METATYPE(HubSight::Admin::PasskeyPage)
Q_DECLARE_METATYPE(HubSight::Admin::PasskeyOptions)
Q_DECLARE_METATYPE(HubSight::Admin::PasskeyOperationResult)
Q_DECLARE_METATYPE(HubSight::Admin::PasskeyLoginResult)
