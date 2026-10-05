# OpenNOW — native client (C++ + Dear ImGui)

A from-scratch C++17 reimplementation of the OpenNOW GeForce NOW client: the
whole UI, the whole app state machine, and the streaming engine, rendered with
[Dear ImGui](https://github.com/ocornut/imgui) 1.91.4.

The React/Electron client this ports lives one directory up
(`../src/client`, ~78k lines). Nothing is linked against it — this is a
standalone binary with **zero third-party runtime dependencies**: Dear ImGui is
vendored under `third_party/imgui`, the OpenGL entry points are loaded by our
own loader (`include/onow/gfx/GLLoader.h`), TLS is OpenSSL on POSIX and
Schannel on Windows, and there is no package manager step anywhere in the
build.

```
native/
├── include/onow/     public headers, one directory per layer
├── src/              implementation, mirroring the headers
├── tools/            i18n code generator + key audit
├── tests/            test runner
├── docs/             architecture, streaming, UI, testing
└── third_party/imgui Dear ImGui 1.91.4 (core + backends), vendored
```

---

## Build

```sh
make                       # auto-detects: win32 on Windows, glfw if GLFW is present, else headless
make PLATFORM=win32        # Win32 + WGL OpenGL 3.2 core, MSVC or MinGW
make PLATFORM=glfw         # GLFW + OpenGL 3.2 core (Linux/macOS/BSD)
make PLATFORM=headless     # no window, no GL — for tests and CI
make test                  # build + unit checks + i18n key audit
make i18n                  # regenerate the translation tables
make check-keys            # audit every tr() key against locales/en.json
make clean
```

`PLATFORM=win32` needs only the Windows SDK (`cl /EHsc` or MinGW `g++`).
`PLATFORM=glfw` needs `glfw3` and `libGL`; nothing else.

CMake works too (`cmake -S . -B build && cmake --build build`), but the
Makefile is the one that is exercised in CI — `cmake` is not installed in this
environment.

### Requirements

| Platform | Compiler | Libraries |
| --- | --- | --- |
| Windows | MSVC 2019+ or MinGW-w64 | none (opengl32/ws2_32/secur32/crypt32/winmm/gdi32/user32/shell32/advapi32/imm32) |
| Linux/macOS | g++ 9+ or clang 10+ | `glfw3`, `libGL`, `libssl`/`libcrypto` |
| Any (tests) | same | none |

## Run

```sh
./build/headless/opennow                       # or build/glfw/opennow, build/win32/opennow
./build/headless/opennow --backend-url http://127.0.0.1:3000
./build/headless/opennow --locale de
./build/headless/opennow --headless --max-frames 120   # render N frames, then quit
./build/headless/opennow --self-test           # 84 pure-logic checks, no window, no network
./build/headless/opennow --version
```

`--backend-url` defaults to `http://127.0.0.1:3000` (the OpenNOW backend in
`../src/server`) and can also come from `OPENNOW_BACKEND_URL`.

## What is implemented

| Area | Where | Notes |
| --- | --- | --- |
| App state machine | `src/app/App.cpp`, `AppCommands.cpp`, `AppState.h` | the whole `App.tsx` state machine, minus React |
| Views | `src/app/views/*.cpp` | Login, Home/Store, Library, Game Details, Playtime, Settings, Stream, toasts |
| Design system | `src/ui/Theme.cpp`, `Widgets.cpp` | palette, metrics, type scale, chamfered panels, every widget |
| Fonts | `src/ui/ImGuiLayer.cpp` | Montserrat/Segoe/Helvetica + a mono face, loaded from the OS with a built-in fallback |
| Backend client | `src/services/GfnClient.cpp` | auth, providers, regions, subscription, catalog, library, sessions, pings, community queue |
| Services | `src/services/*.cpp` | device-login polling, session create/poll/claim/stop/resume, catalog cache, playtime ledger |
| HTTP/TLS | `src/net/Http.cpp`, `TlsSocket*.cpp` | OpenSSL on POSIX, Schannel on Windows |
| WebSocket | `src/net/WebSocket.cpp` | RFC 6455 client, TLS or plaintext |
| Streaming — WebRTC | `src/stream/WebRtcEngine.cpp` | SDP/ICE over the signaling bridge |
| Streaming — NVST | `src/stream/NvstEngine.cpp`, `Process.cpp` | stdio JSON-lines sidecar protocol, telemetry, commands |
| i18n | `src/i18n/*` | all 12 locales, generated from `../locales` |
| Persistence | `src/app/Settings.cpp`, `Fs.cpp` | `settings.json`, `runtime.json`, catalog/playtime caches |
| Platform | `src/platform/*.cpp` | Win32, GLFW and headless backends behind one contract |

## What is *not* implemented

Being explicit about the boundary, because a port that overstates itself is
worse than one that does not exist:

- **Video decode.** `WebRtcEngine` negotiates the session and tracks every
  packet, frame and statistic, but there is no H.264/H.265/AV1 decoder and no
  GPU upload path. It runs in a documented "counting stub" mode: the transport
  behaves correctly and reports the truth, but no pixels are produced. Wiring a
  decoder means implementing the `VideoDecoder` hook in
  `include/onow/stream/WebRtcEngine.h`.
- **Audio.** WASAPI capture/playback is a stub (`ONOW_AUDIO_NONE` on POSIX).
- **The native streamer binary.** `NvstEngine` speaks the full stdio protocol
  to the vendored Rust engine in `../third_party/opennow-streamer`, but that
  crate is not built here; point `settings.nativeStreamerExecutablePath` at a
  built binary and the handshake runs.
- **Gamepad/HID.** Input events are modelled and forwarded, but no
  DirectInput/SDL/evdev backend reads a physical pad.
- **Screenshots/recordings.** The shortcuts, paths and UI plumbing exist; the
  frames they would capture do not (see video decode).

Everything else — navigation, the entire settings surface, catalog browsing and
search, the free-tier server picker, the playtime ledger, toasts, modals,
keyboard navigation, locale switching, persistence, the launch lifecycle, the
queue state machine, resume, and the styling — is complete.

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — layers, ownership rules, threading
- [docs/STREAMING.md](docs/STREAMING.md) — the two transports and their contracts
- [docs/UI.md](docs/UI.md) — the design system, widget set and view mapping
- [docs/TESTING.md](docs/TESTING.md) — what the tests cover and how to run them

## License

The OpenNOW client is GPL-3.0. Dear ImGui is MIT; see
`third_party/imgui/LICENSE.txt`.
