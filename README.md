# HubSight Admin SDK for Qt/C++

Desktop SDK for building HubSight professional Admin applications with Qt 6 and
C++20.

The SDK owns the native transport and session plumbing so normal application code
does not need to implement or directly manage:

- Qt's native HTTP request lifecycle;
- JWT access/refresh-token rotation;
- Socket.IO or standard JSON WebSocket framing;
- reconnect, replay, and realtime authentication;
- WebRTC peer-connection state management.

Supported targets are **Windows, macOS, and Linux desktop**. Android, iOS,
mobile, and tablet deployments are intentionally out of scope.

## Current status

The repository contains the REST/auth foundation, the complete Admin API v1
catalog, the normative Admin realtime relay, the Socket.IO compatibility layer,
and the backend-neutral WebRTC foundation. Initial typed Phase 2 REST slices for
live, archive, and notifications are also available.

The SDK is usable for desktop application integration, but it is not yet a full
media/VMS UI product. Native WebRTC media engines, playback/rendering, caching,
and several Admin CRUD domains remain separate follow-up work.

## Requirements

- Qt 6.6 or newer:
    - Qt Core;
    - Qt Network;
    - Qt WebSockets;
    - Qt Test when building tests.
- C++20 compiler.
- CMake 3.21 or newer.
- Windows, macOS, or Linux desktop.

The installed CMake target is:

```cmake
HubSight::AdminSdk
```

For the supported Windows, Linux, and macOS architecture matrix, see
[`docs/BUILD_CROSS_PLATFORM.md`](docs/BUILD_CROSS_PLATFORM.md).

## Current status

## Build

```sh
cmake -S . -B build \
  -DHUBSIGHT_ADMIN_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### CMake options

| Option                                         |            Default | Description                                                                                            |
| ---------------------------------------------- | -----------------: | ------------------------------------------------------------------------------------------------------ |
| `HUBSIGHT_ADMIN_BUILD_TESTS`                   | `${BUILD_TESTING}` | Build the Qt Test suite.                                                                               |
| `HUBSIGHT_ADMIN_ENABLE_HSCFG_IMPORT`           |               `ON` | Enable the native `.hscfg` importer when OpenSSL Crypto, libargon2, libzip, and libyaml are available. |
| `HUBSIGHT_ADMIN_ENABLE_DESKTOP_SECURE_STORAGE` |               `ON` | Enable Windows Credential Manager, macOS Keychain, or Linux Secret Service/libsecret integration.      |

The `.hscfg` importer is optional at configure time. If its dependencies are
not available, the SDK still builds but returns
`HscfgImportError::ImporterUnavailable` instead of accepting an unverified
configuration file.

Desktop secure storage is also detected automatically. Linux support requires
`libsecret-1`. When the OS vault is unavailable, the default application facade
fails closed; it never falls back to plaintext storage.

## Zed / clangd configuration

The repository includes `.clangd` and `.zed/settings.json` for C++/Qt editing in
Zed. CMake is configured to generate `build/compile_commands.json`, which gives
clangd the real Qt include paths, C++20 flags, Qt module defines, AUTOMOC
include directory, and target architecture.

Open the `qt-sdk` directory itself as the Zed workspace root, then configure the
local build once:

```sh
cmake -S . -B build \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

On macOS Apple Silicon with Homebrew Qt, use the Qt prefix if CMake cannot find
Qt automatically:

```sh
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

After changing Qt kits or architectures, delete and recreate `build/` so
`compile_commands.json` is regenerated for the correct target. In Zed, restart
the `clangd` language server or reload the workspace. The generated compilation
database is intentionally ignored by Git because it contains machine-specific
absolute paths.

## Recommended application API

Use `AdminApplicationClient` for normal application and UI code. It provides:

- one high-level configuration entry point;
- automatic JWT refresh based on `expires_in`;
- optional 2FA without exposing the pre-auth token;
- typed system, camera, live, archive, and notification clients;
- high-level live-session orchestration with automatic heartbeat/release;
- normative Standard JSON realtime through `realtime()`;
- bounded, sanitized diagnostics;
- no `QNetworkReply`, `AuthManager`, `SocketIoClient`, `WebRtcClient`, or native
  transport objects in the normal app-facing surface.

```cpp
#include <hubsight/admin/hubsight_admin.h>

#include <QDebug>
#include <QJsonDocument>
#include <QObject>
#include <QUrl>

using namespace HubSight::Admin;

AdminApplicationClient client;
client.setAutoConnectRealtime(true);
client.setDiagnosticLoggingEnabled(true);

QObject::connect(&client, &AdminApplicationClient::twoFactorRequired,
                 [&client] {
                     // Obtain the code from the UI. The pre-auth token stays
                     // inside the SDK.
                     client.verifyTwoFactor(QStringLiteral("123456"));
                 });

QObject::connect(&client, &AdminApplicationClient::authenticated,
                 [](AdminUser user) {
                     qInfo() << "Signed in as" << user.username;
                 });

QObject::connect(&client, &AdminApplicationClient::diagnosticOccurred,
                 [](SdkDiagnostic diagnostic) {
                     qInfo().noquote()
                         << QJsonDocument(diagnostic.toJson()).toJson();
                 });

if (!client.configure(QUrl{"https://gateway.example.com"},
                      QStringLiteral("admin-desktop-key"))) {
    // Handle client.errorOccurred or inspect client.lastDiagnostic().
    return;
}

client.realtime()->subscribeTopic(QStringLiteral("camera.updated"));
client.signIn(QStringLiteral("admin"), QStringLiteral("password"));
```

For a production deployment, prefer importing a validated Admin `.hscfg` profile:

```cpp
AdminApplicationClient client;

if (!client.importHscfgFile(QStringLiteral("admin.hscfg"),
                            QStringLiteral("123456"))) {
    // Handle client.errorOccurred and sanitized diagnostics.
    return;
}

client.signIn(QStringLiteral("admin"), QStringLiteral("password"));
```

The facade also exposes:

```cpp
client.refreshSession();       // Usually automatic; useful after resume.
client.signOut();
client.connectRealtime();
client.disconnectRealtime();
client.system();
client.cameras();
client.live();
client.archive();
client.notifications();

// Live REST negotiation, heartbeat, profile, QoE, and release are owned by
// the facade. The app only observes typed session signals.
QObject::connect(&client, &AdminApplicationClient::liveSessionStarted,
                 [](LiveSession session) {
                     qInfo() << "Live signaling ready for" << session.cameraId;
                 });
QObject::connect(&client, &AdminApplicationClient::liveError,
                 [](QString sessionId, AdminError error) {
                     qWarning() << "Live error" << sessionId << error.serverCode;
                 });
client.startLive(QStringLiteral("camera-1"), QStringLiteral("balanced"));
// client.stopLive(sessionId) when the view is closed.
```

`liveSessionStarted` means the Admin live session has been negotiated and its
signaling data is available. If the media transport is WebRTC and no native
backend adapter is installed, the same session emits `liveError` with
`WEBRTC_BACKEND_UNAVAILABLE`; this is an explicit capability diagnostic, not a
false media-connected state. `AdminApplicationClient` automatically sends
heartbeats for managed sessions and releases them on stop, sign-out, or client
reconfiguration. Reconfiguration/logout waits for release acknowledgements up
to a bounded five-second teardown deadline; if the deadline expires, the SDK
closes local state and records `LIVE_TEARDOWN_TIMEOUT` instead of blocking the
UI thread. If a reconfiguration or `.hscfg` import is queued behind live
teardown, its synchronous `bool` return means “accepted/queued”; observe
`configurationApplied(bool)` or `hscfgImportCompleted(bool)` for the final
result. `clearConfiguration()` emits `configurationCleared()` after its
teardown has completed.

`AdminClient` remains available as an advanced/core API for integrations that
need direct access to lower-level clients. Its native transport and
`AuthManager` APIs are intentionally more exposed and require more application
responsibility.

## Configuration: `.hscfg` as the single source of truth

The Admin `.hscfg` v2 importer is the preferred configuration path. It accepts
only the Admin profile:

- magic: `HSCFG\x02`;
- format: `2.0`;
- profile: `admin_api`;
- API namespace: `/api/admin/v1`;
- Standard realtime namespace: `/relay/admin/v1`;
- authentication: `bearer_jwt_plus_api_key`;
- audience: `admin_desktop` / `admin_api`.

After successful validation and decryption, the profile supplies the endpoint
configuration for:

- HTTP Admin API;
- normative Standard JSON relay;
- Socket.IO compatibility transport;
- WebRTC signaling/media endpoint sources.

The import is applied as one SDK-level configuration operation. Existing auth,
HTTP requests, realtime connections, and WebRTC sessions are invalidated before
the new configuration is applied.

Security properties:

- the PIN and decrypted archive remain memory-only;
- API keys are sent in `X-API-Key`, never in URLs or query strings;
- URLs containing user-info, query credentials, or fragments are rejected;
- production endpoints must use HTTPS/WSS; loopback HTTP/WS is allowed only for
  local tests;
- content hashes are checked and Ed25519 verification can be required by
  configuring a trusted public key and full-integrity mode.

See [`docs/HSCFG_IMPORT.md`](docs/HSCFG_IMPORT.md) for the container format,
native dependencies, and integrity policy.

## Authentication and token storage

The SDK uses the HubSight Admin dual-auth model:

- `X-API-Key` is sent on bootstrap and protected requests;
- `Authorization: Bearer <access-token>` is sent only on protected requests;
- cookies and query-string credentials are not used.

`AdminApplicationClient` manages the complete JWT lifecycle:

- username/password sign-in;
- optional 2FA verification;
- refresh-token rotation;
- automatic refresh before expiration;
- session restore after configuration;
- logout and local session invalidation.

Storage policy:

- access tokens are held in memory only;
- refresh tokens are stored through `DesktopSecureStorage` by default;
- Windows uses Credential Manager;
- macOS uses Keychain Services;
- Linux uses Secret Service through libsecret when available;
- unavailable vaults fail closed with sanitized diagnostics;
- there is no plaintext file, `QSettings`, custom registry, or custom encrypted
  file fallback.

`InMemorySecureStorage` is intended only for tests and explicitly controlled
short-lived development processes. The advanced `AdminClient` constructor uses
in-memory storage unless a `SecureStoragePtr` is injected; the recommended
`AdminApplicationClient` constructor selects `DesktopSecureStorage` by default.

## HTTP transport

The SDK sends Admin requests only below:

```text
/api/admin/v1
```

For HTTPS gateways, Qt negotiates HTTP/2 through TLS ALPN when supported and
falls back automatically to HTTP/1.1 when necessary. The SDK does not use direct
HTTP/2 or h2c and application code does not select the protocol.

Low-level integrations can observe the selected protocol through:

```cpp
QObject::connect(&adminClient, &AdminClient::requestCompleted,
                 [](QString operation, HttpProtocol protocol) {
                     // HttpProtocol::Http2 or HttpProtocol::Http1_1
                 });
```

Every request receives an SDK-generated `X-Request-ID`. HTTP errors are parsed
from the Admin error envelope, maintenance responses preserve `Retry-After`,
and stale responses from an earlier configuration/auth generation are ignored.

## Realtime transports

HubSight has two intentionally separate realtime protocols.

### Standard JSON relay — normative Admin realtime API

The normative Admin relay is a plain JSON WebSocket at:

```text
/relay/admin/v1
```

It does **not** use Engine.IO or Socket.IO framing. The typed domain layer
supports:

- documented Admin topic allowlisting;
- acknowledged `subscribe`, `unsubscribe`, `resume`, and `ping` commands;
- camera, pool, NVR, member, vision, notification, operation, and security
  events;
- reconnect with bounded backoff;
- topic resubscription;
- `event_id` tracking;
- REST snapshot reconciliation after reconnect;
- best-effort replay reporting;
- security events such as `session.revoked`, `auth.force_logout`, and
  `admin_api.disabled`.

Normal app code uses:

```cpp
client.realtime()->subscribeTopic(QStringLiteral("camera.updated"));
```

The application can load an authoritative REST snapshot when notified that
reconciliation is required, then request replay through the typed relay API.

### Socket.IO compatibility layer

`AdminClient::realtime()` exposes the lower-level `SocketIoClient`, and
`AdminClient::socketIoRealtime()` exposes its optional domain layer for
compatible/legacy deployments, currently using `/relay`.

The base supports:

- Engine.IO v4 WebSocket handshake;
- Socket.IO namespaces;
- heartbeat ping/pong;
- JSON events;
- outbound and server-requested acknowledgements;
- bounded reconnect and room rejoin;
- authenticated WebSocket headers without URL credentials.

Polling fallback and binary attachments are not implemented. The Socket.IO
compatibility layer must not be pointed at `/relay/admin/v1`.

## Typed REST clients

The current typed clients are exposed through `AdminClient` and the application
facade:

| Client               | Current coverage                                                                                                                                                            |
| -------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `SystemClient`       | Status, capabilities, and settings.                                                                                                                                         |
| `CameraClient`       | Cursor/limit camera list and detail/status reads.                                                                                                                           |
| `LiveClient`         | Capabilities, camera discovery/status, WebRTC session negotiation, heartbeat, release, profile changes, stats, and QoE. The facade adds managed live-session orchestration. |
| `ArchiveClient`      | Timeline, available days, recording metadata, playback/download/thumbnail URL actions.                                                                                      |
| `NotificationClient` | Listing/filtering, detail, read-state mutations, deletion actions, test dispatch, push configuration, and push-subscription management.                                     |

Responses use typed DTOs and emit domain-specific signals. Archive playback,
thumbnail caching, range seeking, and media rendering are not included in the
REST clients.

## WebRTC foundation

The SDK includes a backend-neutral WebRTC foundation for live sessions:

- typed ICE server/configuration models;
- SDP and ICE candidate DTOs;
- track and connection-state models;
- session registry keyed by live `session_id`;
- `WebRtcPeerConnectionBackend` adapter boundary.

Qt provides network and WebSocket primitives but does not provide an
`RTCPeerConnection` media engine. The SDK therefore does not bundle
libdatachannel, native WebRTC, FFmpeg, hardware decoding, or rendering. An
approved backend adapter is required before offer/answer or ICE operations can
run; otherwise the SDK reports `BackendUnavailable` through its structured error
and diagnostics paths.

See [`docs/WEBRTC_FOUNDATION.md`](docs/WEBRTC_FOUNDATION.md).

## Endpoint catalog and deferred endpoints

`adminEndpointCatalog()` contains all 136 entries from the HubSight Admin API v1
catalog, including method, path, authentication, permission, and target phase.
`AdminClient::api()` exposes one forward-compatible placeholder method for each
catalog entry.

Catalog methods that have not reached a typed implementation currently emit:

```text
SDK_ENDPOINT_NOT_IMPLEMENTED
```

They deliberately do not send a network request. The catalog is therefore the
complete planned API surface, while typed clients represent the endpoints that
are currently implemented and validated.

## Diagnostics and debugging

Use `AdminApplicationClient` diagnostics instead of inspecting native network or
WebRTC objects:

```cpp
client.setDiagnosticLoggingEnabled(true);

QObject::connect(&client, &AdminApplicationClient::diagnosticOccurred,
                 [](SdkDiagnostic diagnostic) {
                     qInfo().noquote()
                         << QJsonDocument(diagnostic.toJson()).toJson();
                 });
```

Each `SdkDiagnostic` can contain:

- UTC timestamp;
- severity and source;
- stable diagnostic code;
- operation name;
- request ID when available;
- retryability;
- safe structured details.

Diagnostic history is bounded in memory. Credential values, request bodies,
Bearer tokens, API keys, passwords, and raw sensitive response data are not
included by default. Qt logging uses the category:

```text
hubsight.admin.sdk
```

Useful diagnostic codes include `AUTHENTICATED`, `TOKEN_REFRESHED`,
`SECURE_STORAGE_UNAVAILABLE`, `SECURE_STORAGE_READ_FAILED`,
`SECURE_STORAGE_WRITE_FAILED`, `RELAY_CONNECTION_FAILED`,
`WEBRTC_BACKEND_UNAVAILABLE`, `REPLAY_UNAVAILABLE`, and
`TOPIC_OPERATION_FAILED`.

## Security and protocol boundaries

The SDK deliberately enforces these boundaries:

- Admin REST uses `/api/admin/v1`, never the legacy App API namespace;
- Standard Admin realtime uses plain JSON WebSocket at `/relay/admin/v1`;
- Socket.IO is only a separate compatibility transport;
- WebRTC signaling/media is not tunneled through Socket.IO;
- credentials are not placed in URLs or query strings;
- cookies and ambient browser credentials are disabled;
- refresh tokens are not written to plaintext application settings;
- `.hscfg` decrypted contents are not persisted by the importer.

## Tests

The contract-oriented Qt Test suite uses local HTTP/1.1 and WebSocket servers to
verify:

- Admin URL namespace and dual-auth headers;
- HTTP/1.1 protocol reporting and HTTP/2 negotiation configuration;
- JWT rotation, persisted-session restore, and fail-closed storage;
- secure-storage round-trip behavior where the desktop vault is available;
- `.hscfg` validation and single-source endpoint configuration;
- Standard relay topic, reconnect, replay, and security behavior;
- Socket.IO handshake, heartbeat, events, acknowledgements, and security events;
- typed live, archive, notification, and WebRTC foundation behavior;
- application-facade diagnostics and transport ownership;
- complete endpoint catalog/stub behavior;
- maintenance responses and retry metadata.

Run the suite with:

```sh
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

A full live HTTP/2 integration test requires a TLS test server with ALPN
support. The production path uses Qt's native HTTP/2 negotiation and does not
require a separate direct/h2c mode.

## Documentation

- [`docs/BUILD_CROSS_PLATFORM.md`](docs/BUILD_CROSS_PLATFORM.md) — Windows 10/11
  amd64/ARM64, Linux amd64/ARM64, and macOS arm64 build and deployment guide.
- [`docs/PHASE1.md`](docs/PHASE1.md) — delivered REST, auth, realtime, facade,
  diagnostics, and foundation work.
- [`docs/HSCFG_IMPORT.md`](docs/HSCFG_IMPORT.md) — Admin `.hscfg` v2 format,
  dependencies, validation, and integrity policy.
- [`docs/RELAY_FOUNDATION.md`](docs/RELAY_FOUNDATION.md) — normative Standard
  JSON relay contract and replay/reconciliation behavior.
- [`docs/WEBRTC_FOUNDATION.md`](docs/WEBRTC_FOUNDATION.md) — backend-neutral
  WebRTC adapter boundary.

The source contract is maintained in the HubSight repository documents
referenced by `docs/PHASE1.md`.
