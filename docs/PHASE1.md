# Phase 1 — REST and authentication foundation

## Source contract

The implementation follows the repository documents:

- `../../hubsight/docs/ADMIN_API_QT_SDK_UNIFIED_PLAN.md`
- `../../hubsight/docs/ADMIN_API_V1_ENDPOINT_CATALOG.md`

The backend currently exposes the Admin namespace through the gateway and
validates the dedicated `admin_desktop` / `admin_api` client binding. The SDK
therefore does not reuse the legacy `/api/app/v1` contract. This SDK targets
Windows, macOS, and Linux desktop applications only; mobile and tablet targets
are not supported.

## Delivered

### Complete Admin API endpoint surface

`adminEndpointCatalog()` contains all 136 entries from
`ADMIN_API_V1_ENDPOINT_CATALOG.md`, including HTTP method, path template,
authentication requirement, permission, and target phase. `AdminClient::api()`
exposes a callable generic method for every catalog entry. HTTP entries resolve
path/query/body fields and send real requests through the shared Admin transport;
there is no `SDK_ENDPOINT_NOT_IMPLEMENTED` fallback anymore.

Typed clients now cover all REST resource groups in the catalog: account/profile,
system operations, camera management, members/faces/uploads, identity,
integrations, and the earlier live/archive/notification clients. The generic
client remains useful for forward-compatible fields and server additions. The
catalog's `/relay/admin/v1` entry is intentionally handled by the Standard JSON
relay domain, not by HTTP.

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

Access tokens are held in memory. Refresh tokens are written only through
`SecureStorage`. `AdminApplicationClient` defaults to `DesktopSecureStorage`,
which uses Windows Credential Manager, macOS Keychain Services, or Linux Secret
Service/libsecret. If the OS vault is unavailable, writes fail closed rather
than falling back to plaintext. `InMemorySecureStorage` is deliberately limited
to explicit test/development injection.

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
fallback and binary attachments remain outside this base transport. The client is
not connected automatically.

### Socket.IO realtime domain layer

`SocketIoRealtimeClient`, exposed through `AdminClient::socketIoRealtime()`,
provides the optional compatibility domain layer for the Socket.IO gateway used
by current relay deployments. It manages additional room subscriptions,
acknowledged join/leave lifecycle, reconnect/rejoin behavior, generic event
normalization, and typed signals for camera, notification, pool, NVR, member,
vision, and operation events. `session:revoked` and `auth:force_logout` are
security events: `AdminClient` invalidates the local auth session, closes
WebRTC peers, and disconnects Socket.IO and standard relay transports.

The layer does not recreate the authenticated user/role/session rooms assigned by
the gateway. It also does not replace `StandardRelayClient`: the normative Admin
realtime contract is plain JSON WebSocket at `/relay/admin/v1`, while this
Socket.IO API targets the compatible Socket.IO gateway (currently `/relay`).
The Standard relay domain layer is exposed separately through
`AdminClient::relayRealtime()` and does not use Socket.IO framing.

### Initial Phase 2 live REST slice

The first Phase 2 slice is now available through `AdminClient::live()`:

- live capabilities and cursor-based live camera listing;
- WebRTC session negotiate/heartbeat/release/change-profile;
- session stats, QoE reporting, and camera status.

The client uses typed live DTOs and the existing dual-auth Admin transport.
Standard JSON relay events and replay are provided by
`AdminClient::relayRealtime()`; native media-engine integration remains a
separate layer.

### Initial Phase 2 archive REST slice

`AdminClient::archive()` now implements the normative archive endpoints with
protected dual authentication:

- timeline query by `from`, `to`, optional `camera_id`, cursor, and limit;
- available recording days by camera/year/month;
- recording metadata;
- playback, download, and thumbnail URL action requests.

The typed parsers accept the documented `data` envelope, flat objects, and
legacy-compatible flat timeline array shapes while retaining normalized `raw`
objects. The SDK uses the catalog paths under `/api/admin/v1/archive`. Backend
route/schema parity can be verified with the opt-in integration target documented
in `INTEGRATION_TESTING.md`. Native media playback, range seeking, thumbnail
cache, and WebRTC/media integration are not part of this REST slice.

### Complete typed Admin REST resource coverage

The typed Admin resource layer now covers all HTTP endpoint families in the
catalog. In addition to the live/archive/notification slices described above,
`AccountClient`, `SystemOperationsClient`, `CameraManagementClient`,
`MemberClient`, `IdentityClient`, and `IntegrationClient` provide validation,
request correlation, typed response DTOs, and domain-specific signals. `MemberClient`
supports JSON/presign flows plus multipart image and binary avatar payloads.

The SDK uses the Admin action paths and current push-subscription resource from
the catalog. Legacy notification routes are intentionally not used. Run the
opt-in backend integration suite to verify the deployed Admin notification and
push-subscription routes against the catalog contract.

### Admin `.hscfg` v2 importer

`HscfgImporter` accepts only the Admin `HSCFG\x02` profile and performs native
Argon2id/AES-GCM decryption, bounded in-memory ZIP/YAML parsing, URL and
identity validation, content-hash checking, and optional Ed25519 verification.
`AdminClient::importHscfg()` applies the result to HTTP, Socket.IO foundation,
standard JSON relay, and WebRTC endpoint sources as one configuration operation.

### Application-facing facade

`AdminApplicationClient` is the recommended entry point for desktop/UI code.
It owns configuration, JWT login/refresh/logout (including automatic refresh
from `expires_in`), the optional 2FA pre-auth state, automatic Standard relay
connection, and typed resource access. It does
not expose `AdminTransport`, `QNetworkReply`, `AuthManager`,
`SocketIoClient`, or `WebRtcClient` as part of the normal app surface. Socket.IO
remains an internal compatibility capability and the normative realtime API is
`AdminApplicationClient::realtime()`.

`SdkDiagnostic` provides a bounded history and `diagnosticOccurred()` signal
with timestamp, source, operation, stable code, retryability, and request ID
when available. Diagnostic serialization deliberately excludes credentials and
request bodies. `setDiagnosticLoggingEnabled(true)` writes the same sanitized
records to the `hubsight.admin.sdk` Qt logging category. Native WebRTC/media
engine integration remains an SDK/platform capability; applications receive a
structured diagnostic instead of needing to manage the peer registry or native
adapter directly.

### Standard JSON relay domain layer

`AdminClient::relay()` exposes `StandardRelayClient`, while
`AdminClient::relayRealtime()` exposes the typed domain layer for the normative
plain JSON WebSocket contract at `/relay/admin/v1`. The domain layer enforces
the documented topic allowlist, sends acknowledged `subscribe`, `unsubscribe`,
`resume`, and `ping` commands, parses the event envelope into `RelayEvent`, and
routes typed camera, pool, NVR, member, vision, operation, notification, and
security signals. It retains the latest `event_id`; after reconnect it requests
REST snapshot reconciliation before the application calls `requestResume()`.
Unavailable replay is reported as a best-effort result rather than a fatal
transport error. It intentionally has no generic room or client broadcast API.

### WebRTC foundation

The SDK now includes a backend-neutral WebRTC foundation for Phase 2 live
sessions. `WebRtcConfiguration`, SDP, ICE candidate, track, and state DTOs are
JSON-serializable; `WebRtcPeerConnectionBackend` is the adapter contract for a
future libdatachannel/native engine; and `AdminClient::webrtc()` manages peer
connections by `session_id`. No media engine is bundled, and an adapter is
required before offer/answer or ICE operations can run. Details are in
[`WEBRTC_FOUNDATION.md`](WEBRTC_FOUNDATION.md).

### Resource surface and remaining scope

`SystemClient` covers `status`, `capabilities`, and `settings`; `CameraClient`
covers camera list/detail reads; their mutation and operational companions are
provided by `SystemOperationsClient` and `CameraManagementClient`. All catalog
HTTP methods are therefore available either through a typed resource client or
through `AdminClient::api()`.

The following are intentionally outside the Admin REST endpoint completion:

- WebRTC media-engine integration/libdatachannel, FFmpeg, hardware decoding,
  and live matrix;
- asynchronous thumbnail cache, native archive playback, and range-seeking;
- Socket.IO polling fallback and binary attachments;
- domain-specific `.hscfg` profile management beyond the Admin `admin_api`
  profile;
- QML controls and sample VMS UI.

## Validation

`tests/test_admin_sdk.cpp` is a contract-oriented Qt Test suite. It uses local
HTTP/1.1 and WebSocket servers to verify the Admin URL namespace, dual-auth
headers, absence of cookie/query credentials, HTTP protocol fallback reporting,
Socket.IO handshake/event/ack handling, Socket.IO room/domain events and
security invalidation, typed live, archive, and notification REST slices,
`.hscfg` profile rejection, application-facade auth/diagnostics behavior,
standard relay transport/domain/replay behavior, generic endpoint request
routing, multipart upload, typed live/archive/notification behavior, and the
maintenance response.
A machine with Qt 6.6+ is required to configure and execute it. The optional
backend smoke suite is documented in `INTEGRATION_TESTING.md` and requires a
provisioned gateway/API key. A full HTTP/2 integration test additionally requires
a TLS test server with ALPN support; the production path uses Qt's native
negotiation rather than a separate direct/h2c mode.
