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

Realtime domain clients/event replay, the WebRTC media engine, FFmpeg, QML
views, thumbnails, archive playback, CRUD resources, and `.hscfg` cryptography
are intentionally deferred to later phases. The Socket.IO transport and
backend-neutral WebRTC foundations are included so Phase 2 can add live
signaling without an API redesign. See [`docs/PHASE1.md`](docs/PHASE1.md) and
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

## WebRTC foundation

`AdminClient::webrtc()` exposes a session registry for the Phase 2 live API.
The foundation includes typed ICE server/configuration, SDP, ICE candidate,
track, and connection-state models plus a `WebRtcPeerConnectionBackend`
adapter interface. It does not bundle libdatachannel or another media engine;
applications attach an approved backend before calling `createOffer()` or
`createAnswer()`. Standard JSON signaling remains separate from Socket.IO and
must not use Socket.IO packet framing. See
[`docs/WEBRTC_FOUNDATION.md`](docs/WEBRTC_FOUNDATION.md).
