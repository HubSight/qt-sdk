#pragma once

#include "admin_export.h"

#include <QByteArray>
#include <QHash>
#include <QString>

#include <memory>
#include <optional>

namespace HubSight::Admin {

// Refresh tokens are stored through this interface. The app-facing facade
// uses DesktopSecureStorage by default; custom implementations must provide an
// equivalent OS-protected vault for production. The SDK never logs values read
// from storage.
class HUBSIGHT_ADMIN_EXPORT SecureStorage {
public:
  virtual ~SecureStorage() = default;

  virtual std::optional<QByteArray> read(const QString &key) const = 0;
  virtual bool write(const QString &key, const QByteArray &value) = 0;
  virtual bool remove(const QString &key) = 0;

  // Used only for diagnostics and capability reporting. Implementations must
  // not include secret values in this label.
  virtual QString backendName() const { return QStringLiteral("custom"); }

  // Optional, sanitized backend detail for diagnostics. Implementations must
  // never include token values, keys, or other secrets in this message.
  virtual QString lastError() const { return {}; }
};

// Suitable for tests and short-lived processes. It intentionally does not
// provide persistence or encryption.
class HUBSIGHT_ADMIN_EXPORT InMemorySecureStorage final : public SecureStorage {
public:
  std::optional<QByteArray> read(const QString &key) const override;
  bool write(const QString &key, const QByteArray &value) override;
  bool remove(const QString &key) override;
  QString backendName() const override;

private:
  QHash<QString, QByteArray> m_values;
};

using SecureStoragePtr = std::shared_ptr<SecureStorage>;

} // namespace HubSight::Admin
