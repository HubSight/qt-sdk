#pragma once

#include "admin_export.h"
#include "secure_storage.h"

#include <QString>

namespace HubSight::Admin {

// Desktop-only OS credential vault adapter for refresh-token persistence.
//
// Windows: Credential Manager generic credentials.
// macOS: Keychain Services generic-password items.
// Linux: Secret Service through libsecret when HUBSIGHT_ADMIN_LIBSECRET is
// available at build time.
//
// This class never stores access tokens through the SDK; AuthManager uses it
// only for the rotated refresh token. Unsupported/unavailable backends fail
// writes instead of silently falling back to a plaintext file.
class HUBSIGHT_ADMIN_EXPORT DesktopSecureStorage final : public SecureStorage {
public:
  explicit DesktopSecureStorage(
      const QString &service = QStringLiteral("com.hubsight.admin"));

  static bool isSupported();
  static QString platformBackendName();

  QString service() const;

  std::optional<QByteArray> read(const QString &key) const override;
  bool write(const QString &key, const QByteArray &value) override;
  bool remove(const QString &key) override;
  QString backendName() const override;
  QString lastError() const override;

private:
  void setError(const QString &message) const;
  QString normalizedKey(const QString &key) const;

  QString m_service;
  mutable QString m_lastError;
};

} // namespace HubSight::Admin
