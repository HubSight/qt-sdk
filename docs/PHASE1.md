# Phase 1 — REST and authentication foundation

## Source contract

The implementation follows the repository documents:

- `../../hubsight/docs/ADMIN_API_QT_SDK_UNIFIED_PLAN.md`
- `../../hubsight/docs/ADMIN_API_V1_ENDPOINT_CATALOG.md`

The backend currently exposes the Admin namespace through the gateway and
validates the dedicated `admin_desktop` / `admin_api` client binding. The SDK
therefore does not reuse the legacy `/api/app/v1` contract.

## Delivered

### Complete Admin API endpoint surface

`adminEndpointCatalog()` contains all 136 entries from
`ADMIN_API_V1_ENDPOINT_CATALOG.md`, including HTTP method, path template,
authentication requirement, permission, and target phase. `AdminClient::api()`
exposes one placeholder method per entry. These methods currently emit
`SDK_ENDPOINT_NOT_IMPLEMENTED` and deliberately do not send network requests;
future phases will replace the generic `QJsonObject` request with typed DTOs,
transport calls, validation, and concurrency/idempotency behavior.

The catalog also records the planned standard JSON WebSocket relay at
`/relay/admin/v1`. It is separate from the Socket.IO transport foundation
because the current Admin relay contract explicitly specifies standard JSON
WebSocket, not Socket.IO.

### Transport

- Gateway URL normalization rejects the legacy App API namespace and
  credential-bearing URLs.
- Production gateways must use HTTPS; HTTP is accepted only for loopback tests.
- `QNetworkRequest::Http2AllowedAttribute` is enabled for every request. Qt
  negotiates HTTP/2 over HTTPS/TLS ALPN when the gateway supports it and
  automatically falls back to HTTP/1.1 otherwise. The SDK deliberately does
  not use `Http2DirectAttribute` or h2c/direct HTTP/2.
- The public `AdminClient::requestCompleted` signal reports the operation and
  protocol actually used. Protocol selection remains automatic for callers.
- Reconfiguring the gateway, API key, or secure store invalidates the in-memory
  session and ignores responses from the previous auth generation.
- Requests are built only under `/api/admin/v1`.
- `X-API-Key` is sent on bootstrap and protected calls.
- `Authorization: Bearer <JWT>` is sent only on protected calls.
- Every request receives an SDK-generated `X-Request-ID`.
- Successful network responses are classified as `HttpProtocol::Http2` when
  Qt reports `Http2WasUsedAttribute`; otherwise they are classified as
  `HttpProtocol::Http1_1`. Failed network operations report `Unknown`.
- `Idempotency-Key` is supported by the internal transport for later mutations.
- Cookies are disabled and query-string credentials are never generated.
- HTTP errors use the backend `code`, `error`, `details`, and `request_id`
  envelope.
- `503 ADMIN_API_DISABLED` produces `Maintenance` state and preserves the
  `Retry-After` value.

### Authentication

`AuthManager` provides:

- `login`;
- `verifyTwoFactor`;
- `refresh` with refresh-token rotation support;
- `logout` with best-effort remote revocation;
- `fetchCurrentUser`.

Access tokens are held in memory. Refresh tokens are written only through the
`SecureStorage` interface. The default `InMemorySecureStorage` is deliberately
not suitable for production.

### Socket.IO transport foundation

`SocketIoClient` is available through `AdminClient::realtime()` and can also be
used as a standalone base client. It provides:

- Engine.IO v4 WebSocket handshake and session tracking;
- server ping/client pong heartbeat handling and heartbeat timeout;
- Socket.IO namespace CONNECT/DISCONNECT packets;
- JSON event delivery, outbound acknowledgement tracking, and explicit
  replies to server-requested acknowledgements;
- reconnect with bounded exponential backoff;
- handshake headers plus namespace auth without URL credentials.

The default endpoint path is `/socket.io/`; applications can override it for a
gateway-specific relay path. Through `AdminClient`, the configured API key and
current access token are mirrored to WebSocket handshake headers, and an active
connection is re-established when the access token rotates. Polling
fallback, binary attachments, event replay, and domain-specific realtime clients
are intentionally outside this base layer. The client is not connected
automatically.

### Read-only resources

`SystemClient` currently covers `status`, `capabilities`, and `settings`.
`CameraClient` covers cursor/limit list and detail requests, including the
status/configuration fields currently returned by the backend. Mutations are
omitted until the endpoint DTOs and idempotency/concurrency behavior are
finalized across the Admin API.

## Intentionally deferred

- domain-specific realtime clients on `/relay/admin/v1` and event replay;
- WebRTC/libdatachannel, FFmpeg, hardware decoding, and live matrix;
- asynchronous thumbnail cache and archive playback;
- camera mutation, PTZ, discovery, members, notifications, access governance;
- passkey/WebAuthn login options and verification;
- `.hscfg` v2 decrypt/signature verification;
- production Keychain/Credential Manager/Secret Service adapters;
- QML controls and sample VMS UI.

## Validation

`tests/test_admin_sdk.cpp` is a contract-oriented Qt Test suite. It uses local
HTTP/1.1 and WebSocket servers to verify the Admin URL namespace, dual-auth
headers, absence of cookie/query credentials, HTTP protocol fallback reporting,
Socket.IO handshake/event/ack handling, the complete endpoint registry/stub
contract, and the maintenance response. A machine
with Qt 6.6+ is required to configure and execute it. A full HTTP/2 integration
test additionally requires a TLS test server with ALPN support; the production
path uses Qt's native negotiation rather than a separate direct/h2c mode.
