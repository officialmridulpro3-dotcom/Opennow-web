# OpenNOW Native — C++ / WinUI 3 / DirectX 10

A complete native Windows port of the OpenNOW GeForce NOW client. Same protocol,
same design system, no Electron, no bundled Chromium, no browser.

| | Web / Tauri client | This client |
|---|---|---|
| UI | React + CSS in WebView2 | WinUI 3 (Windows App SDK) XAML, C++/WinRT |
| Video | `<video>` + WebRTC in the browser | **DirectX 10** swap chain in a XAML `SwapChainPanel` |
| Backend | bundled Node server in-process | native C++20 (WinHTTP, no Node) |
| Shell | Rust + WebView2 (~30 MB) | ~8 MB, one process |
| GPU floor | needs WebView2 + ANGLE on D3D11 FL 10_0 | **DirectX 10.0/10.1 device, shader model 4.0** |
| Idle memory | ~250–350 MB | ~90–140 MB |

## What is ported

The whole application, not a shell:

- **Auth** — NVIDIA device authorization (code + browser approval), token
  exchange/polling, refresh, client tokens, JWT claims, Gravatar fallback.
- **Catalog** — LCARS GraphQL with persisted-query hashes, inline-query
  fallback when NVIDIA rotates them, panel → shelf flattening, game mapping
  (stores, ownership, genres, controller support, image sizes).
- **CloudMatch** — session create, network-test session, poll, resume/claim,
  stop, ad acknowledgement, queue position and seat-setup parsing, including
  the WebRTC vs NVST metadata split that locks the transport at allocation.
- **Signaling** — `wss://{seat}/nvst/` with the `x-nv-sessionid.` sub-protocol,
  peer_info / SDP / ICE framing (WinRT `MessageWebSocket`).
- **Subscription & regions** — MES tier lookup, dynamic region list with the
  known-zone fallback, parallel bounded latency probes.
- **Settings** — every section from the web client (account, stream, game,
  audio, input, interface, about, thanks) persisted as JSON; credentials
  encrypted with DPAPI (per Windows account).
- **Playtime** — per-game totals, sessions, last played, persisted to disk.
- **UI** — title bar, navigation rail, header with search and account chip,
  status bar, home shelves + spotlight, library grid with filters, game
  details with region picker, streaming page with HUD and in-session controls,
  settings, playtime, and the sign-in overlay.
- **Localization** — all 12 web locales, generated into `.resw`.

## Layout

```
native/
  OpenNOW.sln
  Directory.Build.props
  src/
    Core/        portable C++20: JSON, hashes, HTTP, GFN protocol, settings
                 (also builds with CMake on Linux — that is where the tests run)
    DirectX/     DirectX 10 video pipeline + D3D11 composition bridge
    App/         WinUI 3 application: shell, views, controls, theme dictionaries
  tools/         code generators that keep the native client in sync with the
                 web client (design tokens, GraphQL queries, locales)
```

Read next: [BUILD.md](BUILD.md) to compile, [ARCHITECTURE.md](ARCHITECTURE.md)
for how the pieces fit, [PORTING_STATUS.md](PORTING_STATUS.md) for exactly what
is finished, stubbed, or intentionally dropped.
