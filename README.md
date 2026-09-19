# HubSight Admin SDK for Qt/C++

Phase 1 foundation for the HubSight professional Admin desktop client.

## Scope

This initial release implements the REST/auth foundation described by the
HubSight Admin API plan:

- Qt 6.6+ (Core, Network, and WebSockets) and C++20 CMake library;
- dedicated gateway namespace: `/api/admin/v1`;
- API-key-only bootstrap requests and dual-auth protected requests;
- in-memory access token plus pluggable secure storage for refresh tokens;
- username/password login, 2FA verification, refresh, logout, and `/auth/me`;
- system status/capabilities/settings clients;
- cursor-based, read-only camera list/detail clients;
- typed live capabilities, camera discovery/status, and WebRTC session
  lifecycle REST client;
- typed archive timeline, available-days, recording metadata, and short-lived
  playback/download/thumbnail URL REST client;
- typed notifications, destructive notification actions, and push configuration
  REST client;
- typed error, maintenance, state, token, user, and resource models;
- complete 136-entry Admin API v1 endpoint catalog;
- `AdminClient::api()` facade with one public placeholder method per catalog
  endpoint; deferred calls emit `SDK_ENDPOINT_NOT_IMPLEMENTED` until their
  phase is implemented;
- no cookies, `X-Client-ID`, query-string credentials, or legacy App API
  fallback;
- HTTP/2 negotiation over HTTPS/TLS ALPN with automatic HTTP/1.1 fallback;
  the SDK does not force direct HTTP/2/h2c and applications do not need to
  select a protocol for ordinary requests.

The WebRTC media engine, FFmpeg, QML views, thumbnail caching, archive
playback integration, and CRUD resources are intentionally staged for later
phases. The Admin `.hscfg` v2 importer, a standard JSON WebSocket relay domain
layer, and an optional Socket.IO compatibility domain layer are included;
Socket.IO must not be used for the Admin JSON relay. See
[`docs/HSCFG_IMPORT.md`](docs/HSCFG_IMPORT.md),
[`docs/RELAY_FOUNDATION.md`](docs/RELAY_FOUNDATION.md),
[`docs/PHASE1.md`](docs/PHASE1.md), and
[`docs/WEBRTC_FOUNDATION.md`](docs/WEBRTC_FOUNDATION.md).

## Build

Qt 6.6 or newer is required.

```sh
cmake -S . -B build -DHUBSIGHT_ADMIN_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The installed target is `HubSight::AdminSdk`.

`AdminClient::api()` is the forward-compatible endpoint surface. Its request
argument is a generic `QJsonObject` placeholder for future typed path/query/body
DTOs; it does not send a network request yet. The canonical method/path/auth/
permission/phase metadata is available through `adminEndpointCatalog()`.

## Minimal usage

```cpp
#include <hubsight/admin/hubsight_admin.h>

using namespace HubSight::Admin;

AdminClient client;
client.setGatewayUrl(QUrl{"https://gateway.example.com"});
client.setApiKey(QStringLiteral("admin-desktop-key"));

QObject::connect(client.auth(), &AuthManager::loginSucceeded,
                 [](TokenSet, AdminUser user) {
                     qInfo() << "Signed in as" << user.username;
                 });

client.auth()->login(QStringLiteral("admin"), QStringLiteral("password"));
```

The SDK keeps the access token in memory. Applications must provide an
OS-backed `SecureStorage` implementation before using refresh-token persistence
in production; `InMemorySecureStorage` is only a test/development default.

For transport observability, `AdminClient::requestCompleted` reports the
operation and protocol actually used (`HttpProtocol::Http2` or
`HttpProtocol::Http1_1`). HTTPS gateways negotiate HTTP/2 through ALPN when
available; gateways without HTTP/2 support, failed ALPN negotiation, and
loopback HTTP test servers use HTTP/1.1 automatically.

## `.hscfg` single-source configuration

`AdminClient::importHscfg()` and `importHscfgFile()` accept only the Admin
variant (`HSCFG\x02`, format `2.0`, profile `admin_api`). On a successful
import, the validated profile atomically supplies:

- the HTTP gateway/API root and API key;
- the standard JSON relay origin/path and the separate Socket.IO foundation;
- the WebRTC media/signaling endpoint source and media port.

The decrypted archive and PIN are kept in memory only. The API key is sent as
`X-API-Key`, never placed in a URL or query string. Native `.hscfg` support is
enabled automatically when OpenSSL Crypto, libargon2, libzip, and libyaml are
available; otherwise the API returns `HSCFG_IMPORTER_UNAVAILABLE` without
silently accepting an unverified file. See
[`docs/HSCFG_IMPORT.md`](docs/HSCFG_IMPORT.md).

## Socket.IO base

`AdminClient::realtime()` exposes a transport-level `SocketIoClient`. It
supports Engine.IO v4 over WebSocket and the Socket.IO v4 foundation needed by
later domain clients:

- Engine.IO open/close, ping/pong, and heartbeat timeout;
- namespace connection and configurable Socket.IO auth payload;
- JSON events, outbound acknowledgement IDs, and explicit replies to
  server-requested acknowledgements;
- reconnect with bounded exponential backoff;
- WebSocket handshake headers without query-string credentials.

The client defaults to `/socket.io/`, does not implement polling or binary event
attachments yet, and is not connected automatically. When accessed through
`AdminClient`, the configured API key and current access token are mirrored to
WebSocket handshake headers; an active realtime connection is re-established
when the access token rotates. Deployments using a custom relay path can call
`setPath()` before `connectToServer()`.

### Socket.IO realtime domain layer

`AdminClient::socketIoRealtime()` adds the backend-compatible domain layer on top
of `AdminClient::realtime()`:

- `subscribeRoom()`/`unsubscribeRoom()` manage additional rooms and emit room
  lifecycle signals after server acknowledgement;
- desired rooms are automatically rejoined after a successful reconnect;
- generic `RealtimeEvent` delivery is routed into typed camera, notification,
  pool, NVR, member, vision, and operation signals;
- `session:revoked` and `auth:force_logout` invalidate the local Admin session,
  close WebRTC sessions, and disconnect the realtime transports through the
  existing `AdminClient` security path.

The backend-created user/role/session rooms are not duplicated by this layer.
This API is for deployments exposing the compatible Socket.IO gateway (currently
`/relay`). The normative Admin realtime contract remains
`AdminClient::relay()` using plain JSON WebSocket at `/relay/admin/v1`; it does
not use Socket.IO framing.

## Standard JSON relay

`AdminClient::relay()` exposes the transport and
`AdminClient::relayRealtime()` exposes its typed domain layer. Both use the
normative `/relay/admin/v1` endpoint with ordinary JSON WebSocket frames: no
Engine.IO/Socket.IO framing, no rooms, and no client broadcast API. The domain
layer enforces the documented allowlist of Admin topics, provides typed event
signals, retains the latest `event_id`, and supports acknowledged
`subscribe`/`unsubscribe`/`resume`/`ping` commands. After an unexpected
reconnect it re-subscribes desired topics and emits
`snapshotReconciliationRequired()`; the application loads its REST snapshot and
then calls `requestResume()`. Replay is best-effort and reports unavailable
history through `replayCompleted(false, reason)`. `session.revoked`,
`auth.force_logout`, and `admin_api.disabled` invalidate the Admin session
through `AdminClient`. See [`docs/RELAY_FOUNDATION.md`](docs/RELAY_FOUNDATION.md).

## Live REST client

`AdminClient::live()` covers the Phase 2 live REST surface:

- `fetchCapabilities()` and cursor-based `listCameras()`;
- `negotiate()`, `heartbeat()`, `release()`, and `changeProfile()`;
- `fetchSessionStats()`, `reportQoe()`, and `fetchCameraStatus()`.

Responses are parsed into `LiveCapabilities`, `LiveCameraPage`, `LiveSession`,
`LiveSessionStats`, and `LiveCameraStatus`. Negotiation remains separate from
media: applications can use `LiveClient::sessionNegotiated` to configure the
WebRTC peer adapter and pass the returned SDP/ICE data through the appropriate
signaling contract.

## Archive REST client

`AdminClient::archive()` implements the normative Admin archive contract:

- `fetchTimeline(from, to, cameraId, cursor, limit)` with ISO-8601 query values;
- `fetchAvailableDays(cameraId, year, month)`;
- `fetchRecording(recordingId)`;
- `requestPlaybackUrl()`, `requestDownloadUrl()`, and `requestThumbnailUrl()`.

Responses are parsed into `ArchiveTimelinePage`, `ArchiveAvailableDays`,
`RecordingSegment`, and `ArchiveUrlResult`. The client uses
`/api/admin/v1/archive/...` and does not substitute the sibling backend's
legacy `/archive/:id/stream`, `/archive/:id/thumbnail`, or
`/archive/:id/available-days` routes. A native playback pipeline, range
seeking, caching, and media-engine integration remain later work.

## Notifications REST client

`AdminClient::notifications()` implements the normative Admin notifications
surface: cursor/filter listing, detail fetch, patch/read state, mark-all-read,
individual/batch/clear deletion, test dispatch, push configuration, and current
push-subscription upsert/removal.

Destructive methods require an explicit confirmation value and default to the
contract literal `yes` when no unique target name exists. The SDK uses action
paths such as `/notifications:read-all`, `/notifications:batch-delete`, and
`/notifications:clear`; it does not substitute the sibling backend's legacy
`/notifications/read-all`, `/notifications/batch`, or `/notifications/:id/read`
routes. Push subscription payloads remain JSON objects so desktop push providers
can supply their provider-specific fields without exposing credentials in URLs.

## WebRTC foundation

`AdminClient::webrtc()` exposes a session registry for the Phase 2 live API.
The foundation includes typed ICE server/configuration, SDP, ICE candidate,
track, and connection-state models plus a `WebRtcPeerConnectionBackend`
adapter interface. It does not bundle libdatachannel or another media engine;
applications attach an approved backend before calling `createOffer()` or
`createAnswer()`. Standard JSON signaling remains separate from Socket.IO and
must not use Socket.IO packet framing. See
[`docs/WEBRTC_FOUNDATION.md`](docs/WEBRTC_FOUNDATION.md).
