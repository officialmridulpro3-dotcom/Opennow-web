# Native stream overlay (styled chrome over the NVST video plane)

## The problem

The native (NVST) engine presents gameplay with D3D11 in its own child window.
On Windows that child window sits **above** the WebView2 surface — WebView2
per-pixel transparency was tried and is not reliable — so anything the React
client renders in the main window is hidden behind the video.

That is the reason the stream menu (Ctrl+G) and the stats strip (Ctrl+N) were
painted by the engine itself with GDI (`DrawTextW`, `Polygon`, `FillRect`,
`UpdateLayeredWindow`). Win32 draws have no gradients, no blur, no layout
engine, no animation and no design tokens, so the result could never look like
the rest of OpenNOW. The old comment in `overlay.rs` — *"chamfered cells,
slanted segments, corner brackets, 96px numeral, live dot"* — is what styling
looks like when every pixel has to be drawn by hand.

## The fix: keep Rust for video, move chrome to the web layer

Rewriting the engine in another language would throw away the parts Rust is
genuinely good at: RTSP/SRTP/SCTP transport, Media Foundation decode, D3D11
present, raw input, WASAPI audio. None of that is a styling problem, and none of
it is slow *because* of Rust.

What was missing was a surface that can be styled. So the chrome moved into one:

```text
┌─ OpenNOW.exe ────────────────────────────────────────────────────────────┐
│  main window (WebView2)              overlay window (WebView2)           │
│  • library, settings, account        • transparent, undecorated          │
│  • owns session state                • always on top, taskbar-hidden     │
│  • drives the overlay                • follows the main client rect      │
│                                      • click-through unless deck is open │
│                                                                          │
│  ┌────────────── native video (D3D11 child window) ──────────────┐       │
│  │  gameplay                                                     │       │
│  └───────────────────────────────────────────────────────────────┘       │
└──────────────────────────────────────────────────────────────────────────┘
```

* **Overlay UI** — `src/client/components/native-overlay/` (React + CSS:
  deck sidebar with Session/Controls/Media/Keys tabs, live stats HUD with a
  bitrate sparkline, toast stack, end-session dialog). Full design-system
  styling, motion, focus rings, keyboard navigation.
* **Overlay window** — `src-tauri/src/native_overlay.rs`. Created hidden at
  startup and follows the main window's client rect on a 200 ms poll. In deck
  mode it covers the whole client area; in HUD-only mode it shrinks to a
  352x470 box in the top-right corner, so the play area is untouched even
  before click-through is taken into account (it is also set click-through
  whenever the deck is closed).
* **Bridge** — `src/client/nativeOverlayHost.ts` (main window ⇒ shell ⇒ overlay)
  and `nativeOverlayChannel.ts` (overlay ⇒ shell). The main window stays the
  single source of truth for session state; the overlay renders it and sends
  actions back.
* **Engine routing** — the vendored NVST engine normally opens its built-in
  panel for Ctrl+G / Ctrl+N / Guide. With `OPENNOW_NATIVE_HOST_OVERLAY=1` (set
  by this repo's backend when it spawns the sidecar) those shortcuts are
  forwarded to the host instead, and the engine's once-per-second `telemetry`
  line is mapped to the client's `native-stream-stats` event so the styled HUD
  shows real numbers (fps, bitrate, ping, packet loss).
* **Deck actions** — end session, fullscreen, capture/release mouse, microphone
  and recording go through `POST /api/native/command`, which the sidecar
  whitelists (`microphone-toggle`, `recording-toggle`, `recording-start/stop`,
  `fullscreen-toggle`). Everything else stays engine- or shell-driven.

## Preview it without Windows or a GeForce NOW account

```bash
npm run preview:overlay
# http://localhost:5173           → overlay preview (synthetic gameplay backdrop,
#                                   mock session + live stats, Ctrl+G / Ctrl+N)
# http://localhost:5173/ui        → the library/settings UI preview
```

The preview runs the **same components** against a mock channel
(`src/client/nativeOverlayPreview.tsx`), so what you see is what the overlay
window renders on Windows.

## Controls

| Shortcut | Action |
|---|---|
| `Ctrl+G` / Guide | toggle the deck |
| `Ctrl+N` | toggle the stats HUD |
| `←` / `→` | switch deck tabs (deck open) |
| `Tab` / `Shift+Tab` | cycle deck controls |
| `Esc` | close the deck (or the exit dialog) |

## Files

| Path | Role |
|---|---|
| `overlay.html`, `src/client/overlay.tsx` | transparent overlay page entry (second Vite entry) |
| `src/client/components/native-overlay/*` | deck, HUD, toasts, dialog, channel, CSS |
| `src/client/nativeOverlayHost.ts` | main-window bridge (commands, pushes, action listener) |
| `src-tauri/src/native_overlay.rs` | overlay window lifecycle, bounds sync, IPC commands |
| `src/server/nativeStream.ts` | `OPENNOW_NATIVE_HOST_OVERLAY`, telemetry → stats mapping, whitelisted commands |
| `third_party/.../core/src/lib.rs` | host-overlay shortcut routing (env-gated, upstream default untouched) |

## Verify on the first Windows run

The Rust shell can only be compiled and exercised on Windows, so these are the
points to check on the first desktop build:

1. **Click-through in HUD mode** — `WebviewWindow::set_ignore_cursor_events`
   should let clicks reach the game. The HUD-only window is deliberately small
   so a failure here costs a 352x470 corner, not the whole screen. If it does
   fail, set `WS_EX_TRANSPARENT` on the overlay HWND directly (the vendored
   engine already shows the `windows-sys` calls to copy).
2. **Transparency** — the overlay must composite over the video plane rather
   than painting a black sheet. The scrim deliberately avoids a full-screen
   `backdrop-filter` for this reason; if a black sheet still appears, drop the
   remaining blurs or fall back to an opaque deck.
3. **Focus hand-off** — opening the deck focuses the overlay, closing it returns
   focus to the main window so the engine re-locks the cursor.

## Follow-ups

* Deck strings are English-only for now; wiring them into `locales/*.json`
  should happen before the overlay ships broadly.
* Screenshots (F11) are still host-side and unimplemented for the native
  transport — the deck deliberately does not advertise them yet.
* The overlay follows the main window's *client rect*; if the native surface is
  ever letterboxed inside the window, the deck will need the same rect the
  surface command reports.
