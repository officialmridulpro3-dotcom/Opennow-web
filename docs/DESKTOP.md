# OpenNOW Desktop (native, not Electron)

OpenNOW Desktop wraps the **unmodified** web client and backend in a native
[Tauri 2](https://v2.tauri.app) shell written in Rust. On Windows it renders
through **WebView2** — the same evergreen Chromium engine the OS already ships
— instead of bundling a private copy of Chromium like Electron does.

The UI is pixel-identical to the web app: same React code, same stylesheet,
same assets. Nothing in `src/client` was changed.

```
┌─ OpenNOW.exe (Rust shell, a few MB) ───────────────────────────────┐
│  • picks a free 127.0.0.1 port                                     │
│  • spawns opennow-server (bundled Node runtime)                    │
│      └─ server.mjs — the whole Express backend bundled to ONE file │
│         (auth, catalog, CloudMatch, signaling bridge, sessions)    │
│  • polls /api/health until the backend is ready                    │
│  • opens a WebView2 window on http://127.0.0.1:<port>              │
│  • kills the backend on exit (+ a Node-side watchdog as backup)    │
└────────────────────────────────────────────────────────────────────┘
        ▲ same-origin HTTP + WebSocket            ▲
        └── dist/ — the built React client ──────┘
```

Loading the client from the backend's own origin is what makes the app work
unchanged: session cookies (`SameSite=Strict`, scoped to `/api`), the
`x-opennow-client` CSRF check, the signaling WebSocket origin check, and the
production CSP all behave exactly as in a normal deployment.

## Why this is faster than Electron (and friendlier to old GPUs)

| | Electron | OpenNOW Desktop |
|---|---|---|
| Renderer | private Chromium copy per app (~150–200 MB) | shared OS WebView2 runtime |
| Host process | Node.js + Chromium + V8 | small Rust binary |
| Typical RAM | 300 MB+ | ~½ or less (webview + lean backend) |
| Install size | 150 MB+ | ~30 MB installer (backend runtime included) |
| Startup | Chromium cold boot | webview reuse + instant loopback loads |

Extra work done for **DirectX 10-class GPUs** specifically:

- The webview launches with `--ignore-gpu-blocklist`. Chromium normally
  demotes old GPUs to software rendering (SwiftShader), which makes the
  blur-heavy UI crawl. ANGLE only needs D3D11 feature level 10_0 — supported
  by every DirectX 10 GPU — so forcing the GPU pipeline is safe and is the
  single biggest win on that hardware.
- `--enable-gpu-rasterization` keeps paint on the GPU.
- `--autoplay-policy=no-user-gesture-required` lets the stream start
  immediately without a click.
- `--disable-background-timer-throttling` and `--disable-renderer-backgrounding`
  keep session polling and stream rendering alive while the window is
  unfocused (alt-tab during matchmaking).
- No transparent window effects (they are expensive on old compositors).
- The Rust shell is built with LTO and size-optimized release settings.

## Requirements

- Windows 10/11 x64 (Windows 7/8.1 work with the final WebView2 runtime
  version 109 — install it manually; the installer's bootstrapper will fail
  on those systems).
- WebView2 Runtime — the installer downloads it automatically if missing.
- A GeForce NOW account for actual streaming (same as the web app).

## Getting the app

### CI build (no toolchain needed)

The repo ships a workflow: **Actions → "Desktop build (Windows)" → Run
workflow**. It produces two artifacts:

- `OpenNOW-windows-x64-installer` — NSIS setup (`OpenNOW_<version>_x64-setup.exe`),
  per-user install (no admin).
- `OpenNOW-windows-x64-portable` — a ZIP you can extract anywhere and run.

The workflow also runs automatically on pushes to `main` and version tags.

### Local build (Windows)

Prerequisites: Node 22+, Rust stable (MSVC toolchain via
[rustup](https://rustup.rs)), and Visual Studio Build Tools (C++ workload).

```powershell
npm ci
npm run desktop:build
```

Output:

- Installer: `src-tauri\target\release\bundle\nsis\OpenNOW_<version>_x64-setup.exe`
- Portable exe: `src-tauri\target\release\opennow.exe` (needs `server.mjs`,
  `dist\`, and the `opennow-server-*.exe` sidecar beside it — the CI
  portable ZIP assembles exactly that layout)

### Desktop development

```powershell
npm run desktop:dev
```

This runs the normal dev stack (`npm run dev`: Express + Vite on
localhost:3000) and opens it in a native debug-build window. No backend
bundling or sidecar is involved in dev mode; hot reload works as usual.

## Data locations

Everything the desktop app writes lives in one folder
(`%APPDATA%\app.opennow.desktop`):

| File/Dir | Purpose |
|---|---|
| `session-secret` | persistent auto-generated cookie-encryption secret (sessions survive restarts) |
| `gfn-cache/` | catalog/thumbnail cache (same `OPENNOW_DATA_DIR` mechanism as the server) |
| `server.log` | backend stdout/stderr — check here first when something misbehaves |
| `launcher.log` | paths the shell resolved (backend exe, bundle, client dir) — include with support requests |

The backend listens on `127.0.0.1` only (never on other interfaces), on a
random free port, so it never conflicts with a dev server or another
instance and never triggers a firewall prompt.

## How the pieces fit (repo map)

| Path | Role |
|---|---|
| `src-tauri/src/main.rs` | Rust shell: backend lifecycle, health gate, window + GPU flags, single-instance, exit cleanup |
| `src-tauri/tauri.conf.json` | window/bundle config (NSIS, WebView2 bootstrapper, resources, sidecar) |
| `src-tauri/capabilities/default.json` | Tauri permissions (opener for external links, dialog for startup errors) |
| `src-tauri/icons/` | app icons generated from `public/opennow-logo.png` (`npm run desktop:icons`) |
| `scripts/prepare-desktop.mjs` | esbuild-bundles `src/server` into `src-tauri/resources/server.mjs` and stages the Node sidecar |
| `src/server/desktopWatchdog.ts` | shuts the backend down if the host process dies unexpectedly |
| `src/server/index.ts` | three env-driven hooks: `OPENNOW_STATIC_DIR`, `OPENNOW_BIND_HOST`, watchdog — web deployments are unaffected |

Generated at build time (gitignored): `src-tauri/resources/server.mjs`,
`src-tauri/binaries/opennow-server-*` (the bundled Node runtime),
`src-tauri/target/`, `src-tauri/gen/`.

## Troubleshooting

- **"OpenNOW failed to start" dialog** — open `server.log` (see Data
  locations) and look at the last lines; the most common cause is antivirus
  quarantining the freshly-copied sidecar.
- **"bundled backend executable (opennow-server) not found"** or
  **"bundled web client (dist) not found"** — two lookup bugs in older
  installers: they expected the `opennow-server-<triple>.exe` name even
  though Tauri ships installed sidecars as plain `opennow-server.exe`,
  and they tested `dist/` with `is_file()`, which is always false for a
  directory. Fixed in current builds: reinstall from a fresh installer or
  portable ZIP. To run the old build without reinstalling, start the
  bundled backend by hand from the install folder and open it in a
  browser (same app, without the Tauri shell):
  `set NODE_ENV=production && opennow-server.exe server.mjs`, then open
  `http://localhost:3000` (use the file's current name if you renamed it).
- **White screen / blank window** — ensure the WebView2 Runtime is installed
  (Microsoft's "WebView2 Evergreen Runtime"); the installer normally handles
  this.
- **Stream won't start** — identical to the web app: check the NVIDIA
  sign-in under the account menu and consult the in-app stream diagnostics
  (F3).
- **Old machine notes** — on Windows 7/8.1 install WebView2 runtime 109
  manually; on very old GPUs the atmosphere shaders fall back automatically
  (the `LazyShaderAtmosphere` component already feature-detects WebGL).

## Native game window keys

The standalone game window owns its own input; these engine shortcuts work
while it is focused (defaults, configurable per launch via the START
context's `shortcuts` map):

- **F11** — toggle borderless fullscreen (video follows the window size, so
  manual windowed resizes scale the picture too).
- **F8** — toggle mouse pointer lock (also re-syncs a stuck-hidden cursor to
  visible so menu motion flows again; the game re-asserts its cursor state
  on its next update).
- **Ctrl+Shift+Q** — quit the native session from the keyboard.
- **Ctrl+G** — overlay menu request (acknowledged; the in-window menu UI
  arrives in the next build, stats on **Ctrl+N** with it).
