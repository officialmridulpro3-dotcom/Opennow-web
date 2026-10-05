# Architecture

Five layers, each depending only on the ones above it. Nothing below `app/`
knows a windowing API exists; nothing above `platform/` knows which OS it is
on.

```
┌──────────────────────────────────────────────────────────────────────┐
│ app/        App, AppState, AppCommands, Settings, views/*            │  the state machine
├──────────────────────────────────────────────────────────────────────┤
│ ui/         Theme, Widgets, ImGuiLayer                               │  the design system
├──────────────────────────────────────────────────────────────────────┤
│ services/   GfnClient, Auth/Catalog/Session/Playtime services        │  the backend contract
├──────────────────────────────────────────────────────────────────────┤
│ net/ gfx/   Http, TlsSocket, WebSocket, Url, Crypto, GLLoader        │  transport + GL
├──────────────────────────────────────────────────────────────────────┤
│ platform/   Platform (Win32 | GLFW | headless)                       │  window, GL, input
├──────────────────────────────────────────────────────────────────────┤
│ model/      Types — the wire model, mirrored from src/shared/gfn     │
└──────────────────────────────────────────────────────────────────────┘
```

## The rules

1. **`app/` owns state, `ui/` owns pixels.** A view function receives
   `(App&, AppState&, const ImVec2& origin, const ImVec2& size)` and draws. It
   never mutates a service directly — it calls a command on `App`, which is the
   only place that talks to services and the engine. `include/onow/app/Views.h`
   is the whole surface a view may use.
2. **Services are synchronous and callback-driven.** Each service owns a worker
   thread and reports through `std::function` callbacks that the app installs
   once. No service call ever blocks a frame; the UI reads a snapshot that the
   service publishes under a mutex.
3. **One platform backend per binary.** `Platform` is an abstract class and
   `src/platform/Platform{Win32,Glfw,Headless}.cpp` each define
   `create_platform()`/`destroy_platform()`. The Makefile and CMakeLists
   compile exactly one of them; compiling two is a link error by design.
4. **No third-party runtime.** ImGui is vendored, GL is loaded through our own
   loader, TLS is the platform's (OpenSSL/Schannel), JSON/URL/HTTP/WebSocket are
   ours. That is why `make` has no configure step.

## The state machine

The web client kept ~40 `useState` hooks in one 4,800-line `App.tsx`. Here the
same state is one struct, `AppState`, which buys two things React could not:

- The launch/queue/stream lifecycle is inspectable and testable without
  rendering anything (see `SelfTest.cpp`).
- The stream engine drives it from its own thread through a single mutex
  (`App::state_mutex()`), instead of through a chain of `setState` calls that
  must be batched by hand.

`View` is the top-level router (`Loading → Login → Home/Library/Details/
Playtime/Settings/Stream`), `StreamStatus` is the launch lifecycle
(`Idle → Queue → Setup → Starting → Connecting → Streaming`), and
`LaunchErrorState`/`StreamWarningState` carry the failure surfaces.

Commands live in `AppCommands.cpp`. They are the only place that mutates state
*and* touches a service, which is what keeps the view files free of I/O:

```cpp
app.start_device_login();      // auth
app.load_catalog();            // catalog
app.initiate_play(game);       // launch (may open the server picker first)
app.play_game(game, variant);  // launch directly
app.stop_stream();             // teardown
app.resume_navbar_session();   // resume a session the navbar found
app.update_setting_bool(...);  // settings
```

## Threading

| Thread | Does | Touches app state |
| --- | --- | --- |
| main | `App::run()` → `poll_services`, `frame`, `render` | yes, under the state mutex |
| service workers (one per service) | HTTP + WebSocket + polling | publish through callbacks |
| engine media thread | RTP receive / sidecar stdout | publishes stats, emits events |
| engine input | forwards queued input events | no |

The engine never calls into the UI directly. It publishes a `StreamStats`
snapshot and emits `StreamEvent`s (`Connected`, `Disconnected`, `Failed`,
`FirstFrame`); `App` translates those into state changes and toasts.

## Where the port came from

| This port | React client |
| --- | --- |
| `src/client/App.tsx` | → `app/App.cpp` + `app/AppCommands.cpp` + `app/AppState.h` |
| `src/client/api.ts` (`window.openNow`) | → `app/App.h` command surface + `services/*` |
| `src/client/types.ts` + `src/shared/gfn/*` | → `model/Types.h` |
| `src/client/styles.css` | → `ui/Theme.{h,cpp}` |
| `src/client/components/*` | → `ui/Widgets.{h,cpp}` + `app/views/*.cpp` |
| `locales/*.json` | → `src/i18n/TranslationData.cpp` (generated) |
| `src-tauri/docs/NATIVE_STREAMER.md` | → `stream/NvstEngine.*` + `docs/STREAMING.md` |
