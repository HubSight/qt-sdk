# Standard JSON relay foundation

`StandardRelayClient` is the Admin SDK transport for the standard JSON WebSocket
relay contract. It is deliberately separate from `SocketIoClient`:

- no Engine.IO handshake;
- no Socket.IO packet prefixes;
- no namespace CONNECT packet;
- no query-string API key or token;
- no cookies or `X-Client-ID`.

The `.hscfg` Admin profile supplies the relay origin and
`/relay/admin/v1` path. Credentials are mirrored into WebSocket opening
handshake headers:

```text
X-API-Key: <api key>
Authorization: Bearer <access token>
```

## Transport and typed domain API

`StandardRelayClient` remains available when an application needs the generic
transport. `send()` sends exactly the supplied JSON object, while
`sendRequest()` is the opt-in correlation helper: it adds a string `request_id`
when the caller has not supplied one and emits `requestCompleted()` when an
incoming JSON object contains a matching `request_id`, `correlation_id`, or
`id`.

For the normative Admin contract, applications should normally use the typed
domain client:

```cpp
AdminClient client;
client.importHscfgFile(path, pin);

auto *relay = client.relayRealtime();
QObject::connect(relay, &RelayRealtimeClient::cameraEvent,
                 [](RelayEvent event) {
                     qInfo() << event.eventId << event.data;
                 });
relay->subscribeTopic(QStringLiteral("camera.updated"));
client.relay()->connectToServer();
```

The domain client sends only the documented Standard JSON commands:

```json
{"command":"subscribe","topics":["camera.updated"]}
{"command":"unsubscribe","topic":"camera.updated"}
{"command":"resume","last_event_id":"evt_123"}
{"command":"ping"}
```

It rejects topics outside the Admin allowlist and intentionally does not expose
generic room joins, arbitrary broadcasts, or client event publishing.

The allowlist is:

```text
admin_api.enabled, admin_api.disabled, auth.force_logout, session.revoked
camera.started, camera.stopped, camera.updated
pool.status.update, nvr.status.update
vision.person.entered, vision.person.update, vision.person.left, vision.log.new
member.face.updated, notification.new
operation.progress, operation.completed, operation.failed
```

Every event is parsed from the normative envelope containing `event_id`,
`schema_version`, `topic`, ISO-8601 `timestamp`, and object `data`. The complete
envelope is retained in `RelayEvent::raw`; the latest event ID is available via
`lastEventId()`.

## Lifecycle

- `connectToServer()` performs a direct WebSocket opening handshake.
- `disconnectFromServer()` is a manual close and disables automatic reconnect
  for that close.
- `reconnect()` waits for the old socket to disconnect before opening a new
  one. This is important during JWT rotation.
- Unexpected closes use bounded exponential backoff. Configure the delay and
  attempt limit with the reconnect setters.
- A WebSocket ping heartbeat is enabled by default and can be adjusted with
  `setHeartbeatInterval()`.

## Reconnect and replay

The domain client retains desired topic subscriptions across an unexpected
transport close. It clears active subscriptions and re-subscribes them after a
new connection. When this is a reconnect, it emits
`snapshotReconciliationRequired(lastEventId)`. The application must load an
authoritative REST snapshot first, then call `requestResume()`. A successful
or unavailable best-effort replay is reported through
`replayCompleted(resumed, reason)`; unavailable history is not treated as a
fatal transport failure.

`session.revoked`, `auth.force_logout`, and `admin_api.disabled` are routed as
security events. Through `AdminClient`, they invalidate the local auth session,
close WebRTC peers, clear desired Standard relay topics and the replay cursor,
and disconnect the realtime transports.

## Protocol boundary

`StandardRelayClient` and `RelayRealtimeClient` use the normative plain JSON
WebSocket endpoint `/relay/admin/v1`. `SocketIoClient` and
`SocketIoRealtimeClient` are a separate compatibility layer for deployments
that expose the legacy Socket.IO gateway, currently `/relay`. Never point the
Standard relay domain client at that gateway and never work around a backend
mismatch by adding Socket.IO framing to `/relay/admin/v1`.
