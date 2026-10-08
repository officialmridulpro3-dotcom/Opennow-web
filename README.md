# OpenNOW Web

OpenNOW Web is a browser-native GeForce NOW client. It reuses the proven catalog, CloudMatch, signaling, input, and WebRTC protocol work from the desktop OpenNOW project without Electron or the native streamer.

The client UI is the official OpenNOW renderer copied from `OpenNOW/opennow-stable/src/renderer/src`, including the original stylesheet, assets, home and library views, settings modal, queue experience, stream loading screens, and in-stream controls. The intentional web-only UI removals are redirect-based sign-in, native-streamer settings, and desktop application updater controls.

## What changed

- Link-based device authorization is the sign-in flow: OpenNOW shows the NVIDIA verification URL and confirmation code, the visitor approves the request in a new tab, and the server keeps polling `https://login.nvidia.com/token` until it can continue. A QR code of the same verification URL stays available behind a "Show QR code" toggle for signing in from a phone. There is no redirect-based NVIDIA sign-in.
- Each visitor's profile, pending sign-in attempt, NVIDIA tokens, and owned stream IDs are stored in compressed AES-256-GCM encrypted, HTTP-only cookies. They are never returned to page JavaScript.
- Session cookies are scoped to `/api`, use `SameSite=Strict`, and are marked `Secure` in production.
- The session layer is stateless: any server instance can handle any visitor when every instance uses the same `SESSION_SECRET`.
- Game video, audio, keyboard, mouse, and controller input use browser WebRTC.
- Region latency is measured from each visitor's browser with bounded HTTPS checks; unreachable regions remain selectable and show a neutral unavailable state.
- NVIDIA signaling is relayed through an ownership-checked same-origin WebSocket.
- Electron updates, native streaming, local media recording, Discord RPC, and desktop window controls are not part of the web app.
- The UI uses a labeled, full-width desktop navigation rail and flatter high-contrast surfaces; it avoids continuous ambient animation, blurred duplicate library artwork, poster hover transforms, and non-functional decoration. The home screen mounts eight cards per shelf (twelve in the all-games preview), with full shelves still one click away; constrained devices automatically use a low-effects profile, and large libraries render in small batches to reduce startup work and memory use.

## Development

Requires Node.js 22 or newer.

```bash
npm install
npm run dev
```

Open `http://localhost:3000`.

To browse the dev server through a tunnel, cloud IDE, or reverse proxy, list the host names Vite should accept:

```bash
OPENNOW_DEV_ALLOWED_HOSTS=.your-tunnel.example,localhost npm run dev
```

## Production

```bash
npm install
npm run build
NODE_ENV=production OPENNOW_ORIGIN=https://your-domain.example SESSION_SECRET=replace-with-at-least-32-random-characters npm start
```

On PowerShell:

```powershell
$env:NODE_ENV = "production"
$env:OPENNOW_ORIGIN = "https://your-domain.example"
$env:SESSION_SECRET = "replace-with-at-least-32-random-characters"
npm start
```

Put the Node server behind a reverse proxy that provides HTTPS and forwards WebSocket upgrades for `/api/signaling`. Browser streaming and microphone access should be served over HTTPS in production.

Important deployment notes:

- Use the same strong `SESSION_SECRET` on every server instance. Sticky sessions and a shared session database are not required.
- Rotating `SESSION_SECRET` signs everyone out immediately. Keep it in your deployment secret manager, never in source control.
- Set `OPENNOW_ORIGIN` to the exact public origin so signaling WebSocket origin checks are strict.
- Do not deploy the Vite development server publicly.

## Desktop app (native, not Electron)

The same client and backend also ship as a native Windows desktop app built
with Tauri 2 (Rust shell + OS WebView2 — no bundled Chromium). The UI is the
unmodified web client, and the shell forces GPU acceleration for
DirectX 10-class graphics cards. See [docs/DESKTOP.md](docs/DESKTOP.md) for
architecture, builds (CI artifacts or local), and troubleshooting.

```bash
npm run desktop:dev      # native window around the dev stack
npm run desktop:build    # Windows installer + portable exe
npm run preview:overlay  # browser preview of the native-stream deck + live stats
```

The stream chrome for native sessions (deck, live stats, toasts) is a
transparent React/CSS overlay window floated above the engine's D3D11 video
plane — see [docs/NATIVE_OVERLAY.md](docs/NATIVE_OVERLAY.md).

**Stream mode** (`Settings → Stream`) picks the player: **Native engine**
(default) runs the bundled NVST engine with its surface clipped inside the app
window and raw-input capture, so the styled React deck draws directly over it
with no GDI chrome and no browser input path; **In-app player** uses browser
WebRTC instead. See [docs/NATIVE_STREAMER.md](docs/NATIVE_STREAMER.md).

## Checks

```bash
npm run typecheck
npm test
npm run build
```

With the development server running, the isolated-session concurrency check can be run with:

```bash
npm run test:load
```

Set `LOAD_TEST_REQUESTS`, `LOAD_TEST_CONCURRENCY`, or `LOAD_TEST_URL` to change its scope.

Full login, queue, and gameplay validation requires a real NVIDIA/GeForce NOW account. The unauthenticated provider discovery and the link-based device authorization start/cancel flow can be tested without entering credentials.
