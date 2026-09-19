#pragma once

#include "../admin_export.h"
#include "hscfg_types.h"

#include <QByteArray>
#include <QString>

namespace HubSight::Admin {

// Imports the Admin-only HSCFG v2 container entirely in memory. The importer
// deliberately has no file-system or persistent-secret responsibilities.
class HUBSIGHT_ADMIN_EXPORT HscfgImporter final {
public:
  HscfgImporter() = default;

  // The public key is optional because current server packages may omit it
  // from metadata. If supplied, it is trusted for Ed25519 verification.
  // Accepted forms are raw 32-byte, hex/base64, or PEM SubjectPublicKeyInfo.
  void setTrustedEd25519PublicKey(const QByteArray &publicKey);
  QByteArray trustedEd25519PublicKey() const;

  HscfgImportResult importAdmin(const QByteArray &container,
                                const QString &pin) const;

private:
  QByteArray m_trustedEd25519PublicKey;
};

} // namespace HubSight::Admin
