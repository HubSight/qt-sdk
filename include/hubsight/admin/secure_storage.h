#pragma once

#include "admin_export.h"

#include <QHash>
#include <QByteArray>
#include <QString>

#include <memory>
#include <optional>

namespace HubSight::Admin {

// Implement this interface with the host OS keychain before storing refresh
// tokens in a production application. The SDK never logs values read from it.
class HUBSIGHT_ADMIN_EXPORT SecureStorage {
public:
    virtual ~SecureStorage() = default;

    virtual std::optional<QByteArray> read(const QString &key) const = 0;
    virtual bool write(const QString &key, const QByteArray &value) = 0;
    virtual bool remove(const QString &key) = 0;
};

// Suitable for tests and short-lived processes. It intentionally does not
// provide persistence or encryption.
class HUBSIGHT_ADMIN_EXPORT InMemorySecureStorage final : public SecureStorage {
public:
    std::optional<QByteArray> read(const QString &key) const override;
    bool write(const QString &key, const QByteArray &value) override;
    bool remove(const QString &key) override;

private:
    QHash<QString, QByteArray> m_values;
};

using SecureStoragePtr = std::shared_ptr<SecureStorage>;

} // namespace HubSight::Admin
