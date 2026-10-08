# Native (NVST) streaming

OpenNOW Desktop can hand a GeForce NOW session to the native Rust NVST engine
instead of decoding it in browser WebRTC. The native game runs in a separate,
maximized SDL/OS window; the Tauri window remains the OpenNOW launcher and
session controller. WebRTC remains available for browsers and machines where
the native sidecar is unavailable.

## Architecture

```text
OpenNOW.exe (Tauri 2 launcher, opaque and maximized)
├── WebView2 — library, account, settings and session controls
└── opennow-server — auth, catalog, CloudMatch and signaling
    └── opennow-nvst.exe — native NVST transport, decode, input and SDL window
```

Flow: Play allocates a CloudMatch session, the backend spawns the sidecar and
writes `hello` + `start` JSON-lines messages to stdin, and the sidecar opens its
own maximized game window when the first frame is ready. Stopping the session
closes that window and returns the user to the launcher.

There is no native video surface embedded in the WebView, no transparent video
hole, no HWND handoff, and no second Tauri overlay WebView. The renderer's
`/api/native/surface` and shell-window-handle routes have been removed so the
native game cannot accidentally attach to the launcher.

## Clean native window and controls

The sidecar runs with these desktop settings:

- `OPENNOW_NATIVE_EXTERNAL_RENDERER=1` — create a visible SDL game window.
- `OPENNOW_NATIVE_SHELL_PLACEMENT=0` — keep it top-level and independent of the launcher.
- `OPENNOW_NATIVE_INPUT_OWNER=native` — use native keyboard, mouse and gamepad capture.
- `OPENNOW_NATIVE_DISABLE_OVERLAY=1` — do not allocate the engine's menu/stats overlay.
- `OPENNOW_NATIVE_HOST_OVERLAY=1` — route host-owned shortcuts through the session controller.

Ctrl+G / Guide and Ctrl+N are consumed without opening a menu or stats overlay.
The SDL overlay manager is not initialized for OpenNOW native sessions. The
launcher remains a separate window and provides a minimal fullscreen control
that auto-hides when idle; its control acts on the native game window.

**F10 toggles fullscreen** in the standalone native window. It is the default
`shortcutToggleFullscreen` binding in client settings and the sidecar's fallback
binding. The sidecar forwards F10 to the launcher over a passive backend event
channel; the launcher then sends an explicit fullscreen state back to the SDL
window and mirrors the acknowledgement in its own UI. Alt+Enter remains a
native fullscreen chord. F8 toggles native mouse capture, and Ctrl+Shift+Q
stops the session.

The game window is maximized on first presentation. F10 switches it to and from
borderless desktop fullscreen; restoring fullscreen returns to the maximized
window. The launcher itself also starts maximized, but remains opaque and
separate from gameplay.

## Choosing the player

`Settings → Stream → Native Streaming` stores `settings.streamClientMode`:

| Mode | Playback | Window |
| --- | --- | --- |
| **Native engine** (default when bundled) | NVST over RTSPS/SRTP/SCTP; native D3D11 presentation and hardware decode | Separate maximized SDL game window; launcher stays available |
| **In-app player** | Browser WebRTC in the `<video>` element | Gameplay and controls stay in the launcher window |

The sidecar owns its media, input and presentation path, so the WebRTC video
client is disposed when a native session takes over; this avoids running two
video decode/composition paths on a low-resource PC. Desktop WebView UI also
starts in the low-effects profile to reduce decoration and background work.

## Session and input routing

The client builds the engine `SessionContext` through the shared
`buildNativeStreamerSessionContext`; the backend (`POST /api/native/start`)
validates it and forces `settings.transportMode = "nvst"`. The backend spawns,
pipes and supervises the sidecar in `src/server/nativeStream.ts`. The Tauri
launcher resolves the bundled executable and passes its path as
`OPENNOW_NVST_SIDECAR`; the installer and portable ZIP both include it.

With no parent window to embed into, the SDL game window receives focus and
keyboard input directly. Native Raw Input / SDL capture sends gameplay input to
the stream. Shortcut bindings are passed in the session context, including the
F10 fullscreen default. The launcher keeps a passive WebSocket open for native
fullscreen, pointer-lock, clipboard and telemetry events; it does not open a
WebRTC negotiation on that socket.

The separate launcher view can still end the session and expose its minimal
fullscreen control. It does not render over the game window. Native Ctrl+G and
Ctrl+N requests are discarded, while WebRTC's in-app sidebar remains available
when the WebRTC player is selected.

## Build and validation

The sidecar lives in `third_party/opennow-streamer` and is built for Windows x64
by the `nvst-sidecar` job in `.github/workflows/desktop-build.yml`. The Windows
job packages both an NSIS installer and a portable ZIP, then publishes them to
the branch's rolling GitHub Release. CI smoke-tests the JSON-lines `hello`
handshake, but cannot validate CloudMatch allocation, GPU decode or real-time
input without an NVIDIA account and Windows hardware.

## Provisioning

CloudMatch fixes transport at allocation time. Native launches use the
reference native client's provisioning (`secureRTSPSupported: true`,
`enhancedStreamMode: 0`, `transport: null`, and no `GSStreamerType=WebRTC` entry)
and echo it on resume claims. The server honors `nvst` only when a bundled
sidecar exists; web deployments always fall back to WebRTC.

Native seats also configure their encoder from `requestedStreamingFeatures`
rather than WebRTC SDP. Native creates send the reference codec, bitrate, vsync,
audio-channel and client-identity values; resume claims echo the same values.

## Startup hardening and constraints

Every sidecar wait is bounded: hello 30 seconds, start acknowledgement 120
seconds, and client fetch 150 seconds. The hello/start budgets can be tuned via
`OPENNOW_NVST_{HELLO,START}_TIMEOUT_MS`; `status.phase` (`handshake` or
`starting`) drives launcher progress.

The upstream engine is young and can expose GPU/driver-specific issues. H.264
is the broadly supported path on DirectX 10-class hardware; newer codecs depend
on the available decoder and driver. Full playback validation still requires a
real account and Windows machine.
