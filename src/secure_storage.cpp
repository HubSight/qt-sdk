#include "../include/hubsight/admin/secure_storage.h"

namespace HubSight::Admin {

std::optional<QByteArray>
InMemorySecureStorage::read(const QString &key) const {
  const auto it = m_values.constFind(key);
  if (it == m_values.constEnd()) {
    return std::nullopt;
  }
  return *it;
}

bool InMemorySecureStorage::write(const QString &key, const QByteArray &value) {
  m_values.insert(key, value);
  return true;
}

bool InMemorySecureStorage::remove(const QString &key) {
  return m_values.remove(key) > 0;
}

} // namespace HubSight::Admin
