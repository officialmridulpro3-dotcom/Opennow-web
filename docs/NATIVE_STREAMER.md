# Native (NVST) streaming plan

Goal: play GeForce NOW sessions through upstream OpenNOW's native Rust NVST
engine (real RTSPS/SRTP/SCTP wire protocol) instead of browser WebRTC,
launched from this repo's Tauri desktop shell. WebRTC stays as the browser
fallback.

## Architecture

```text
OpenNOW.exe (Tauri shell)
├── WebView2 window — library / settings / account (existing web client)
├── WebView2 overlay window — styled stream deck + live stats (transparent,
│   always-on-top, click-through; see NATIVE_OVERLAY.md)
├── opennow-server — auth / catalog / CloudMatch (existing Node backend)
│   └── NEW: /api/native-session → SessionContext JSON for the engine
│   └── NEW: /api/native/command → whitelisted deck actions (mic, recording)
└── opennow-nvst.exe (NEW sidecar: third_party/opennow-streamer standalone)
    └── own D3D11 window, native input + audio; JSON-lines over stdio
```

Flow: Play → backend allocates CloudMatch session → shell spawns the sidecar,
writes `{"type":"hello","protocolVersion":7}` + `{"type":"start","context":…}`
to its stdin → gameplay in the native window → sidecar exit returns to library.

## Why the sidecar shape

- The standalone engine owns its window, input capture (raw input), audio
  (WASAPI), decode (Media Foundation/DXVA), and presentation (D3D11 swapchain).
- No FFI/GPU-interop work: the Qt-embedded texture-sharing path is unnecessary.
- The Windows standalone presenter supports feature levels down to **10.0**,
  so DirectX 10-class GPUs work (H.264 only — no H.265/AV1 on that hardware).

## Phases

- [x] **Phase 0 — Spike.** Upstream engine evaluated (protocol, Windows
  FL10 path, sidecar shape, SessionContext contract). Verdict: viable.
- [x] **Phase 1 — Vendor + CI sidecar build.** `third_party/opennow-streamer`
  pinned at upstream `v1.0.1`; `nvst-sidecar` CI job builds
  `opennow-streamer.exe` on Windows and smoke-tests the `hello` handshake.
- [x] **Phase 2 — Session bridge.** The client builds the engine
  `SessionContext` via the shared `buildNativeStreamerSessionContext`; the
  backend (`POST /api/native/start`) validates the allocation fields and
  forces `settings.transportMode = "nvst"` before handing it to the engine.
- [x] **Phase 3 — Shell lifecycle + Play UI.** The backend spawns, pipes, and
  supervises the sidecar (`src/server/nativeStream.ts`); the shell resolves
  the bundled binary and passes `OPENNOW_NVST_SIDECAR`; StreamView shows a
  "Play in native window" card that tears down WebRTC media so only one
  transport burns CPU. The sidecar ships in the installer (`externalBin`)
  and the portable ZIP.
- [ ] **Phase 4 — Test loop.** Iterate on real hardware (no NVIDIA account/GPU
  in CI) until playback is smooth.
- [x] **Phase 5 — Styled overlay.** The stream deck and live stats left the
  engine's GDI panel and became a React/CSS overlay window floated above the
  video plane: transparent, always-on-top, click-through unless the deck is
  open, driven from the main window over Tauri IPC, with the engine's telemetry
  line feeding real numbers into the HUD. See
  [NATIVE_OVERLAY.md](NATIVE_OVERLAY.md). Preview with `npm run preview:overlay`.

## Choosing the player (stream mode)

`Settings → Stream → Native streaming` stores `settings.streamClientMode`:

| Mode | Player | Stream chrome |
| --- | --- | --- |
| **Native engine** (default) | NVST sidecar (RTSPS/SRTP/SCTP, D3D11 present, DXVA decode) painting into a child surface clipped inside the app window, with the engine owning raw mouse/keyboard input | React/CSS deck in the app window over the native plane (the page goes transparent only where the video shows) |
| **In-app player** | Browser WebRTC into the in-app `<video>` element | React/CSS deck in the app window (sidebar, live stats HUD, video filters) |

The native surface handshake is what keeps the engine inside the app window: the
client posts the video rect plus the Tauri HWND to `/api/native/surface`, and the
CLI re-posts it when the sidecar comes up (the first command can arrive before
the engine accepts commands, and then the engine would keep a window of its
own). While the deck is open the client sends `input-paused`, which releases the
engine's raw-input capture so the deck can be clicked, and clears it again when
the deck closes. If the shell never hands over a valid HWND the engine falls back
to a standalone window and the transparent overlay window from
[NATIVE_OVERLAY.md](NATIVE_OVERLAY.md) carries the chrome instead.

Two independent paths deliver that HWND, because the handshake is the one thing
everything else depends on:

* `invoke("get_window_handle")` — direct Tauri IPC. The shell serves this page
  from `http://127.0.0.1:<port>`, which Tauri treats as a *remote* origin: the
  IPC bridge only exists when a capability lists the origin, which is what
  `src-tauri/capabilities/remote-backend.json` does.
* `<app-data>/window-handle.json` — the shell writes its main-window handle at
  launch (and deletes it on exit); the client reads it through
  `GET /api/native/surface-handle` whenever IPC is missing or denied. The
  backend refuses stale entries and handles whose owning process is gone.

The shell also passes `OPENNOW_NATIVE_SHELL_PLACEMENT=1`, so the engine never
reveals a window of its own while it waits for that handle: the surface stays
hidden and the engine logs
`shell-placement: standalone window suppressed (waiting for the shell HWND)`
instead. Each attach attempt is verified (`GetParent` must return the shell
window) and logged as `External SDL surface attached …` or
`… attach failed: <reason>`; the backend turns those markers into
`surfaceAttached`/`surfaceError` on `/api/native/status`, so the deck can fall
back to opaque chrome with a visible warning instead of showing a dead hole.

Keyboard input takes a different route. The video plane is a child window of the
shell, kept `WS_EX_NOACTIVATE` so clicking it never takes the keyboard away from
the WebView — that is what keeps the deck's own shortcuts (Ctrl+G, Ctrl+N, F11,
Escape) and the cursor working while the game has the mouse. Gameplay keys would
be invisible to the engine in that arrangement, so the engine's dedicated Raw
Input thread registers the keyboard too (`RIDEV_INPUTSINK`, same foreground
check as the mouse) and forwards the samples to the stream; SDL key handling is
disabled so a key is never delivered twice (`external_keyboard`). Fullscreen is
the shell's job: F11 / the deck button / the engine's `toggle-fullscreen`
shortcut all call `set_app_fullscreen` on the Tauri window, and the client
re-publishes the surface rect as the window resizes, so the video fills the
screen.

Because the engine's plane is drawn *above* the WebView on Windows, the client
publishes a rect that stops short of the deck's own panels (sidebar column,
stats HUD, title pill — measured from the DOM and scaled by devicePixelRatio),
and hides the surface entirely while a centred modal is open. Without that the
React chrome would be visible only until the first frame arrived.

The mode drives the claim (`clientMode`/`transportMode`) and the attach path on
launch, resume and recovery (`startNativeFromClaim` vs. opening the signaling
bridge for the in-app player), so the seat's transport always matches the
player. While the sidecar runs, StreamView reduces the app window to a
transparent hole for the engine's surface; when it is idle the app window keeps
the video and the styled deck.

Until a mode is picked in Settings (`streamModeChosen`) the shipped default owns
`streamClientMode`, so upgrades follow the default instead of a mode an older
build happened to persist.

## Provisioning (HTTP 501 lesson)

The seat's transport locks at CloudMatch allocation: `GSStreamerType=WebRTC`
metaData (+ `secureRTSPSupported: false`) provisions the WebRTC stack, and the
seat's RTSPS endpoint then answers the NVST control-channel upgrade with
HTTP 501. Native launches therefore send the reference native client's
provisioning — no `GSStreamerType` entry, `secureRTSPSupported: true`,
`enhancedStreamMode: 0`, `transport: null` — selected by
`settings.transportMode === "nvst"` and echoed on resume claims. The server
honors `nvst` only when a sidecar binary is present
(`resolveLaunchTransportMode`), so web deployments always stay on WebRTC.

## Hang hardening

Every sidecar wait is bounded so a silent engine surfaces as an error with a
retry instead of an infinite spinner: hello 30s, start-ack 120s (tunable via
`OPENNOW_NVST_{HELLO,START}_TIMEOUT_MS`), client fetch 150s. `status.phase`
(`handshake` | `starting`) drives the card's progress line while starting.

## Stream provisioning (stuck-in-setup lesson)

Transport provisioning alone left native seats parked in setup forever: unlike
WebRTC (codec via SDP), the native seat configures its encoder purely from
`requestedStreamingFeatures`, so native creates send the full reference set
(`codec`, `maxBitrateKbps`, `vsync`, `audioChannelCount`, …) plus the reference
client-identity fields (`sdkVersion "2.0"`, `streamerVersion "14"`,
`clientPlatformName "Windows"`, controllers `[2]`, numeric `appId`, …). Resume
claims echo the same values. A native-only poll backstop fails loudly if the
seat never leaves setup once out of queue.

## Constraints / risks

- Upstream engine is young: pin stable tags, expect GPU/driver-specific bugs.
- H.264-only on DirectX 10 hardware; needs WDDM 1.1+ vendor drivers for DXVA.
- Network/region problems affect native and WebRTC equally (same servers).
- Vendored tree must stay byte-identical to upstream (see third_party/README).
