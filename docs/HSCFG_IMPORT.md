# Admin `.hscfg` v2 import

The Admin SDK treats an Admin `.hscfg` profile as the single source of truth for
HTTP, realtime, and WebRTC endpoint configuration.

## Accepted format

Only the Admin container is accepted:

- magic: `HSCFG\x02`;
- metadata version: `2.0`;
- profile: `admin_api`;
- API namespace: `/api/admin/v1`;
- realtime namespace: `/relay/admin/v1`;
- authentication: `bearer_jwt_plus_api_key`;
- platform/audience: `admin_desktop` / `admin_api`.

Legacy `HSCFG\x01` app/mobile packages are rejected before decryption. The
binary layout follows the backend implementation, not the stale v1 table in
the original specification:

```text
[6-byte magic][32-byte Argon2 salt][12-byte GCM nonce][ciphertext + 16-byte tag]
```

Argon2id uses `time=4`, `memory=64 MiB`, `parallelism=2`, and a 32-byte key.
AES-256-GCM uses the exact six-byte magic as AAD.

## Native dependencies

The importer uses established native implementations; it does not implement
cryptography or ZIP parsing itself:

- OpenSSL Crypto: AES-256-GCM and Ed25519 verification;
- `libargon2`: backend-compatible Argon2id;
- `libzip`: bounded in-memory ZIP reading;
- `libyaml`: YAML scalar mapping parsing.

CMake enables the importer automatically when all dependencies are detected.
If any dependency is absent, the SDK still builds its base transport and
returns `HscfgImportError::ImporterUnavailable` from the importer. To make the
configuration explicit, use:

```sh
cmake -S . -B build \
  -DHUBSIGHT_ADMIN_ENABLE_HSCFG_IMPORT=ON \
  -DHUBSIGHT_ADMIN_BUILD_TESTS=ON
```

Install the dependencies through the platform package manager before building
when native import is required.

## Validation and security

Import is memory-only and validates:

1. six decimal PIN format;
2. AES-GCM authentication;
3. exact Admin ZIP allow-list (`metadata.yml`, `urls.yml`, `key.yml`, optional
   `ca_cert.pem`);
4. Admin metadata, identity, namespace, and authentication fields;
5. production HTTPS/WSS URL policy, with HTTP/WS permitted only for loopback
   tests;
6. no URL user-info, query, or fragment credentials;
7. unified gateway origin for REST and WebRTC signaling;
8. content SHA-256 over the backend-defined payload order;
9. Ed25519 when a usable metadata or trusted public key is available.

The current backend packager may emit an Ed25519 signature without embedding a
public key in metadata. In that case the importer reports
`HscfgIntegrityState::SignatureUnavailable` while still requiring and checking
the content hash. Applications requiring full authenticity should inject the trusted key with
`AdminClient::setHscfgTrustedEd25519PublicKey()` (or directly on an
`HscfgImporter`) and call `setHscfgRequireFullIntegrity(true)`. The applied
status is also available through `importedConfigIntegrity()`.

## Applying to `AdminClient`

```cpp
AdminClient client;
if (!client.importHscfgFile(path, pin)) {
    // Inspect AdminClient::errorOccurred; PIN and API key are never included.
    return;
}

QUrl gateway = client.gatewayUrl();
StandardRelayClient *relay = client.relay();
```

A successful import invalidates the old auth session, closes active realtime
and WebRTC sessions, cancels HTTP requests, and applies the new configuration
as one SDK-level operation. Calling `setGatewayUrl()` or `setApiKey()` later
exits imported-config mode and deliberately makes those manual values the
active configuration source.
