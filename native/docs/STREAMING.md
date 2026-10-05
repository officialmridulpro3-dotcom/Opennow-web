# Streaming

Two completely separate transports, exactly as in the web client. The seat's
transport is locked at CloudMatch allocation time, so the client cannot switch
mid-session — it picks one at launch and honours it for the session's life.

```
   launch ──► claim session ──► build SessionContext ──► engine.start(ctx)
                                                     │
                        ┌────────────────────────────┴───────────────────────────┐
                        ▼                                                        ▼
             WebRtcEngine (webrtc)                                  NvstEngine (nvst)
             SDP/ICE over the signaling WS                          RTSPS/SRTP/SCTP
             ─────────────────────────────                          via a stdio sidecar
```

`settings.transportMode` selects the path (`"webrtc"` or `"nvst"`).
`App::create_stream_engine()` builds the right one and `App::ensure_stream_engine()`
rebuilds it when the setting changes between launches.

## The SessionContext

`App::build_session_context(session)` turns a claimed `SessionInfo` into the
engine's `SessionContext` (`include/onow/stream/StreamEngine.h`). The rules it
follows are the ones the web client's bridge followed:

- **Endpoints.** `rtsps_endpoint` comes from the seat's `rtspsEndpoints` (native
  seats) or the streaming base URL (`https://{zone}.cloudmatchbeta.nvidiagrid.net/`,
  built by `buildGfnZoneStreamingBaseUrl`). `signaling_url` comes from the
  session, and from the resume snapshot when re-attaching.
- **Profile.** The negotiated profile wins over the requested one: resolution
  and fps come from `negotiatedStreamProfile` when present, otherwise from the
  entitlement (`state.entitled_profile()`) and finally from settings. Bitrate is
  `maxBitrateMbps × 1000`.
- **Input channel.** The seat's `mediaConnectionInfo` address, when reported.
- **Auth.** ICE server credentials are carried in `auth_token`; the session id
  doubles as the session key.

`SessionContext::to_json()` emits the field names documented in
`../docs/NATIVE_STREAMER.md`, so a context produced here can be piped to the
real Rust streamer unchanged for debugging.

## WebRTC transport

`WebRtcEngine` owns:

1. The signaling WebSocket (`session.signalingUrl`) — connects, sends the offer,
   receives the answer, exchanges ICE candidates, and answers the backend's
   `requestKeyframe`.
2. An `RtpReceiver` for the media: SRTP off the wire, payloads handed to a
   `VideoDecoder`, per-frame timestamps for jitter stats.
3. Input: key/mouse/wheel/gamepad events queued and forwarded on the data
   channel.

The decoder seam is `VideoDecoder` (`open(width, height, codec)` /
`decode(rtp_payload, length, timestamp_us)`). With no decoder installed the
engine runs in counting-stub mode: it connects, negotiates, reports
`connected`, counts every media byte and frame, and publishes honest statistics
— so the UI, the stats overlay and the diagnostics log all behave as if the
stream were live, minus pixels. See `WebRtcEngine.h` for the full note.

Provisioning note (from `../docs/NATIVE_STREAMER.md`): a seat provisioned for
WebRTC (`GSStreamerType=WebRTC` + `secureRTSPSupported: false`) answers the
NVST control-channel upgrade with HTTP 501, so the transport choice has to be
made *before* allocation — which is why `App::play_game` bakes it into
`build_stream_settings_json()` rather than switching later.

## NVST transport

`NvstEngine` supervises a child process (the vendored Rust streamer) and speaks
JSON-lines over its stdio. `Process.cpp` is a small Win32 + POSIX child
supervisor with piped stdin/stdout/stderr and no shell in between.

**stdin** (one JSON object per line):

| type | payload |
| --- | --- |
| `hello` | `{protocolVersion: 7}` |
| `start` | `{context: <SessionContext JSON>}` |
| `shutdown` | — |
| `surface` | `{x, y, width, height, visible, deviceScaleFactor, showStats, windowHandle}` |
| `microphone-toggle` | `{paused}` |
| `fullscreen-toggle` | `{fullscreen}` |
| `pointer-lock-toggle` | `{locked}` |
| `recording-start` / `recording-stop` | `{path}` |
| `shell-fullscreen` | `{fullscreen}` |

**stdout**:

| type | payload |
| --- | --- |
| `ready` | engine up, protocol accepted |
| `ack` | command acknowledged |
| `telemetry` | `bitrateMbps`, `framesPerSecond`, `framesDecoded`, `framesDropped`, `pingMs`, `packetLossPercent` |
| `error` | `{code, message}` |
| `overlay-request` | the engine wants the overlay shown/hidden |
| `shortcut-action` | an engine-level shortcut fired |
| `fullscreen-state` | `{fullscreen}` |
| `recording-state` | `{recording, path}` |

**stderr** markers the supervisor watches for, because they are the only signal
that media is actually flowing: `inbound first datagram`,
`first {H264,H265,AV1} access unit`, `External SDL surface attached`.

**Bounded handshakes.** A silent engine must surface as an error with a retry,
not an infinite spinner: `hello` waits 30 s, `start` waits 120 s (both tunable
with `OPENNOW_NVST_HELLO_TIMEOUT_MS` / `OPENNOW_NVST_START_TIMEOUT_MS`), and the
client fetch is bounded at 150 s.

## The free-tier server picker

Free-tier NVIDIA accounts on a non-alliance zone get to choose their seat before
launch, which is what `../src/shared/gfn/printedWaste.ts` documents:

1. `initiate_play` sees: NVIDIA provider, not an alliance base URL,
   `!settings.hideServerSelector`, `FREE` tier, idle stream.
2. `GfnClient::printed_waste_queue()` GETs the community queue
   (`https://api.printedwaste.com/gfn/queue/`).
3. The response fills `state.queue_servers` and opens the picker modal.
4. Picking a zone launches with
   `play_game(game, "", "https://{zone}.cloudmatchbeta.nvidiagrid.net/")`.

Any failure — service down, blocked, no eligible zone — falls back to launching
with default routing, which is what the web client did. The `nuked` flag the
picker would ideally show comes from a server-side mapping endpoint that has no
known URL, so the picker treats it as unknown rather than guessing.

## Resume

`App::resume_navbar_session()` re-claims a session the navbar discovered,
rebuilds the same context (echoing the provisioning fields, as the reference
client does on resume claims) and re-attaches the engine. The runtime snapshot
(`runtime.json` in the app-data dir) is written on shutdown so a crashed session
can be resumed — the same contract the web client's `beforeunload` handler
provided.
