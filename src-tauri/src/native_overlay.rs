//! Native stream overlay window — the styled chrome that floats above the
//! NVST video plane.
//!
//! ## Why a second window
//!
//! The native (NVST) engine presents gameplay with D3D11 into a child window
//! parented to the Tauri main window. That child window is always above the
//! WebView2 surface (per-pixel transparency is not reliable on the WebView2
//! side), which is exactly why the stream menu used to be painted by the engine
//! itself with GDI rectangles — the React UI in the main window is simply
//! covered by the video.
//!
//! The fix is not to rewrite the engine's video path (Rust/D3D11 is the right
//! tool for decode + present at 1440p60), it is to render the *chrome* where
//! styling is unlimited: in a transparent web view that the shell floats on top
//! of everything. This module owns that window:
//!
//!   * created hidden at startup, transparent, undecorated, always-on-top,
//!     excluded from the taskbar, never focused until the deck opens,
//!   * follows the main window's client area (position + size) on a cheap
//!     200 ms poll — no per-frame work, no Win32 hooks,
//!   * is click-through (`set_ignore_cursor_events`) unless the deck is open,
//!     so gameplay input keeps flowing while only the stats HUD is on screen.
//!
//! ## Message flow
//!
//! ```text
//! engine (NVST sidecar) ──stdout──▶ Node backend ──WS──▶ main window
//!                                                          │  native_overlay_command / push
//!                                                          ▼
//!                                               shell (this module) ──▶ overlay window
//!                                                          ▲
//!                                    native_overlay_action / state
//! ```
//!
//! The main window stays the single source of truth for session state; the
//! shell only moves visibility requests and payloads between the two web views.

use std::sync::Mutex;
use std::time::Duration;

use serde_json::{json, Value};
use tauri::{
    AppHandle, Emitter, Manager, PhysicalPosition, PhysicalSize, WebviewUrl, WebviewWindowBuilder,
};

/// Window label of the transparent overlay web view.
pub const LABEL: &str = "overlay";
/// Host → overlay payloads (session, stats, toggles, toasts, commands).
pub const DATA_EVENT: &str = "opennow:native-overlay-data";
/// Overlay → host actions ("end-session", "screenshot", …).
pub const ACTION_EVENT: &str = "opennow:native-overlay-action";

/// How often the overlay window re-checks the main window's client rect.
const SYNC_INTERVAL: Duration = Duration::from_millis(200);

#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
enum Mode {
    /// Nothing to show — the overlay window is hidden.
    #[default]
    Hidden,
    /// Deck open: interactive, focused, game input paused by the web layer.
    Deck,
    /// Stats HUD only: visible but click-through, never focused.
    Hud,
}

impl Mode {
    fn parse(deck_open: bool, hud_visible: bool) -> Self {
        if deck_open {
            Mode::Deck
        } else if hud_visible {
            Mode::Hud
        } else {
            Mode::Hidden
        }
    }
}

#[derive(Default)]
pub struct OverlayRuntime {
    mode: Mutex<Mode>,
    /// Commands that arrived before the overlay web view reported readiness.
    pending: Mutex<Vec<String>>,
    ready: Mutex<bool>,
}

impl OverlayRuntime {
    fn mode(&self) -> Mode {
        *self.mode.lock().unwrap_or_else(|error| error.into_inner())
    }

    fn set_mode(&self, mode: Mode) {
        *self.mode.lock().unwrap_or_else(|error| error.into_inner()) = mode;
    }

    fn queue_or_deliver(&self, app: &AppHandle, command: &str) {
        let ready = *self.ready.lock().unwrap_or_else(|error| error.into_inner());
        if ready {
            let _ = app.emit_to(
                LABEL,
                DATA_EVENT,
                json!({ "kind": "command", "command": command }),
            );
            return;
        }
        let mut pending = self.pending.lock().unwrap_or_else(|error| error.into_inner());
        // Only the last visibility command matters while the page boots.
        if command.contains("deck") || command.contains("hud") {
            pending.retain(|queued| {
                !(queued.contains("deck") && command.contains("deck"))
                    && !(queued.contains("hud") && command.contains("hud"))
            });
        }
        pending.push(command.to_owned());
    }

    fn flush(&self, app: &AppHandle) {
        *self.ready.lock().unwrap_or_else(|error| error.into_inner()) = true;
        let queued: Vec<String> = std::mem::take(
            &mut self
                .pending
                .lock()
                .unwrap_or_else(|error| error.into_inner()),
        );
        for command in queued {
            let _ = app.emit_to(
                LABEL,
                DATA_EVENT,
                json!({ "kind": "command", "command": command }),
            );
        }
    }
}

/// Creates the overlay window (hidden) and starts the rect-following loop.
pub fn setup(app: &AppHandle, origin: &str) -> Result<(), Box<dyn std::error::Error>> {
    app.manage(OverlayRuntime::default());

    let url: tauri::Url = format!("{origin}/overlay.html").parse()?;
    let mut builder = WebviewWindowBuilder::new(app, LABEL, WebviewUrl::External(url))
        .title("OpenNOW overlay")
        .inner_size(1280.0, 800.0)
        .decorations(false)
        .resizable(false)
        .transparent(true)
        .shadow(false)
        .always_on_top(true)
        .skip_taskbar(true)
        .focused(false)
        .visible(false)
        .background_color(tauri::window::Color(0, 0, 0, 0));

    #[cfg(target_os = "windows")]
    {
        builder = builder
            .theme(Some(tauri::Theme::Dark))
            .additional_browser_args(crate::BROWSER_ARGS);
    }

    let overlay = builder.build()?;
    // Click-through until the deck asks for input; the stats HUD must never
    // swallow a mouse click aimed at the game.
    let _ = overlay.set_ignore_cursor_events(true);

    let handle = app.clone();
    std::thread::spawn(move || {
        let mut applied: Option<(Mode, i32, i32, u32, u32, bool)> = None;
        loop {
            std::thread::sleep(SYNC_INTERVAL);
            let mode = handle.state::<OverlayRuntime>().mode();
            let Some(overlay) = handle.get_webview_window(LABEL) else {
                continue;
            };
            let Some(main) = handle.get_webview_window("main") else {
                continue;
            };

            if mode == Mode::Hidden {
                if overlay.is_visible().unwrap_or(false) {
                    let _ = overlay.hide();
                }
                applied = None;
                continue;
            }

            if main.is_minimized().unwrap_or(false) {
                if overlay.is_visible().unwrap_or(false) {
                    let _ = overlay.hide();
                }
                applied = None;
                continue;
            }

            let (Ok(position), Ok(size)) = (main.inner_position(), main.inner_size()) else {
                continue;
            };
            let interactive = mode == Mode::Deck;
            let wanted = (mode, position.x, position.y, size.width, size.height, interactive);
            if applied == Some(wanted) {
                continue;
            }

            // Hide while moving keeps the topmost window from trailing the
            // main window with a visible lag; it is shown again right after.
            let _ = overlay.set_position(PhysicalPosition::new(position.x, position.y));
            let _ = overlay.set_size(PhysicalSize::new(
                size.width.max(1),
                size.height.max(1),
            ));
            let _ = overlay.set_ignore_cursor_events(!interactive);
            if !overlay.is_visible().unwrap_or(false) {
                let _ = overlay.show();
            }
            applied = Some(wanted);
        }
    });

    Ok(())
}

/* -------------------------------- commands ------------------------------- */

/// Main window → shell: change what the overlay shows. `mode` is one of
/// `deck`, `hud`, `hidden` (toggle commands are resolved in the web layer).
#[tauri::command]
pub fn native_overlay_command(
    app: AppHandle,
    runtime: tauri::State<'_, OverlayRuntime>,
    command: String,
) -> Result<(), String> {
    runtime.queue_or_deliver(&app, &command);
    Ok(())
}

/// Overlay → shell: what is currently on screen. Drives visibility,
/// click-through and focus so the engine never sees stray input.
#[tauri::command]
pub fn native_overlay_state(
    app: AppHandle,
    runtime: tauri::State<'_, OverlayRuntime>,
    deck_open: bool,
    hud_visible: bool,
) -> Result<(), String> {
    let mode = Mode::parse(deck_open, hud_visible);
    let previous = runtime.mode();
    runtime.set_mode(mode);

    if let Some(overlay) = app.get_webview_window(LABEL) {
        let _ = overlay.set_ignore_cursor_events(mode != Mode::Deck);
        if mode == Mode::Deck {
            let _ = overlay.show();
            let _ = overlay.set_focus();
        } else if mode == Mode::Hud {
            let _ = overlay.show();
        } else {
            let _ = overlay.hide();
        }
    }

    // Leaving the deck hands the keyboard and mouse back to the game.
    if previous == Mode::Deck && mode != Mode::Deck {
        if let Some(main) = app.get_webview_window("main") {
            let _ = main.set_focus();
        }
    }
    Ok(())
}

/// Overlay → shell → main window: a user action from the deck or a toast.
#[tauri::command]
pub fn native_overlay_action(
    app: AppHandle,
    action: String,
    value: Option<f64>,
) -> Result<(), String> {
    app.emit_to(
        "main",
        ACTION_EVENT,
        json!({ "action": action, "value": value }),
    )
    .map_err(|error| error.to_string())
}

/// Main window → shell → overlay: session info, live stats, toggles, toasts.
#[tauri::command]
pub fn native_overlay_push(app: AppHandle, message: Value) -> Result<(), String> {
    app.emit_to(LABEL, DATA_EVENT, message)
        .map_err(|error| error.to_string())
}

/// Overlay → shell: the page finished booting; flush queued commands.
#[tauri::command]
pub fn native_overlay_ready(
    app: AppHandle,
    runtime: tauri::State<'_, OverlayRuntime>,
) -> Result<(), String> {
    runtime.flush(&app);
    Ok(())
}

/// Main window → shell: the session ended, tear the overlay down.
#[tauri::command]
pub fn native_overlay_hide(
    app: AppHandle,
    runtime: tauri::State<'_, OverlayRuntime>,
) -> Result<(), String> {
    runtime.set_mode(Mode::Hidden);
    if let Some(overlay) = app.get_webview_window(LABEL) {
        let _ = overlay.set_ignore_cursor_events(true);
        let _ = overlay.hide();
    }
    Ok(())
}
