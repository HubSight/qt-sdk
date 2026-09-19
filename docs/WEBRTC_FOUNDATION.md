# WebRTC foundation for Phase 2

The SDK now contains a backend-neutral WebRTC foundation for the Admin live
session APIs. It deliberately does not bundle a media engine: Qt 6 provides
WebSocket and network primitives, but it does not provide an
`RTCPeerConnection` implementation.

## Layers

- `WebRtcConfiguration`, `WebRtcIceServer`, `WebRtcSessionDescription`, and
  `WebRtcIceCandidate` are typed, JSON-serializable signaling DTOs.
- `WebRtcPeerConnectionBackend` is the adapter boundary for libdatachannel,
  native WebRTC, or another approved engine.
- `WebRtcPeerConnection` owns one adapter and tracks SDP, ICE, signaling, and
  connection states.
- `WebRtcClient` owns peer connections by the live API's `session_id`.
- `AdminClient::webrtc()` exposes the registry without coupling it to the
  Socket.IO base or the standard JSON `/relay/admin/v1` client.
- `AdminApplicationClient` provides the normal app-facing live orchestration:
  `startLive`, `stopLive`, profile/QoE operations, managed heartbeats, and
  release on lifecycle changes. It does not expose the registry or native
  backend to ordinary app code.

Example setup:

```cpp
using namespace HubSight::Admin;

WebRtcConfiguration configuration;
configuration.iceServers = {
    WebRtcIceServer{{QStringLiteral("stun:stun.example.com")}, {}, {}}
};

WebRtcPeerConnection *peer = client.webrtc()->createPeerConnection(
    sessionId, configuration, std::make_unique<MyWebRtcBackend>());
if (peer) {
    QObject::connect(peer, &WebRtcPeerConnection::localDescriptionCreated,
                     [](WebRtcSessionDescription description) {
                       // Send description.toJson() through the live signaling
                       // contract, not through Socket.IO framing.
                     });
    peer->createOffer();
}
```

`MyWebRtcBackend` must implement the methods on
`WebRtcPeerConnectionBackend` and emit the asynchronous result/state signals.
The SDK rejects ICE server URLs containing credentials in user-info or
unsupported query parameters; TURN credentials are passed through the
separate `username` and `credential` fields.

No backend is installed by default. Calling `createOffer`, `createAnswer`, or
ICE methods without an adapter emits `WebRtcErrorCode::BackendUnavailable`.
This prevents Phase 1 from presenting a signaling-only object as a working
media connection.

## Phase 2 boundary

`AdminClient::live()` obtains `session_id`, ICE server configuration, and
negotiation payloads from `/api/admin/v1/live/*`. `AdminApplicationClient` adds
session ownership and heartbeat/release orchestration on top of that typed
client. `liveSessionStarted` means signaling/session negotiation completed; it
does not mean that a media track is rendering. On logout or reconfiguration,
release acknowledgements are awaited up to a bounded five-second deadline;
late/stale signaling responses are ignored and released rather than promoted
back into the application session registry.

If the negotiated transport is WebRTC and no adapter is installed, the facade
emits `liveError` with `WEBRTC_BACKEND_UNAVAILABLE` and a WebRTC diagnostic.
This is intentional: the SDK never reports a false media-connected state. The
error is non-retryable until an adapter is installed; applications can still
retain the typed signaling session for an approved integration. A future
adapter integration can attach the typed session signaling to `WebRtcClient`
without changing the normal application's JWT, HTTP, or Socket.IO
responsibilities.

A separate standard JSON WebSocket relay client carries relay/replay events at
`/relay/admin/v1`. Neither path should use Socket.IO packets. The WebRTC adapter
is responsible for media, ICE gathering, DTLS/SRTP, and platform decode/render
integration; the SDK foundation is responsible for typed state and safe handoff
between those layers.
