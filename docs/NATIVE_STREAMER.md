# Native (NVST) streaming plan

Goal: play GeForce NOW sessions through upstream OpenNOW's native Rust NVST
engine (real RTSPS/SRTP/SCTP wire protocol) instead of browser WebRTC,
launched from this repo's Tauri desktop shell. WebRTC stays as the browser
fallback.

## Architecture

```text
OpenNOW.exe (Tauri shell)
├── WebView2 window — library / settings / account (existing web client)
├── opennow-server — auth / catalog / CloudMatch (existing Node backend)
│   └── NEW: /api/native-session → SessionContext JSON for the engine
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
- [ ] **Phase 1 — Vendor + CI sidecar build.** `third_party/opennow-streamer`
  pinned at upstream `v1.0.1`; `nvst-sidecar` CI job builds
  `opennow-streamer.exe` on Windows and smoke-tests the `hello` handshake.
- [ ] **Phase 2 — Session bridge.** Backend endpoint translating CloudMatch
  allocation → engine `SessionContext`
  (`session{sessionId,subSessionId,serverIp,mediaConnectionInfo,connectionInfo}`
  + `settings` + `shortcuts`). Needs live-server testing.
- [ ] **Phase 3 — Shell lifecycle + Play UI.** Spawn/pipe/supervise the sidecar
  per stream; hide/show webview; "streaming in native window" state; error
  surfacing from sidecar events.
- [ ] **Phase 4 — Test loop.** Iterate on real hardware (no NVIDIA account/GPU
  in CI) until playback is smooth, then package the sidecar into the
  installer + portable ZIP.

## Constraints / risks

- Upstream engine is young: pin stable tags, expect GPU/driver-specific bugs.
- H.264-only on DirectX 10 hardware; needs WDDM 1.1+ vendor drivers for DXVA.
- Network/region problems affect native and WebRTC equally (same servers).
- Vendored tree must stay byte-identical to upstream (see third_party/README).
