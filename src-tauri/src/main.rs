//! OpenNOW desktop shell — Tauri 2 host process.
//!
//! Responsibilities:
//!   * pick a free loopback port and spawn the bundled backend
//!     (`opennow-server` sidecar running `resources/server.mjs`),
//!   * wait for the backend's `/api/health` endpoint,
//!   * open the main WebView2 window on the backend origin — the unmodified
//!     web client, served same-origin so auth cookies, the signaling
//!     WebSocket, and CSP all keep working without any client changes,
//!   * keep external links (NVIDIA sign-in, store pages) opening in the
//!     system browser,
//!   * terminate the backend on exit (with a Node-side watchdog as backup
//!     for the reverse case: the host dying without cleanup).
//!
//! Why not Electron: the shell is a small Rust binary that reuses the OS
//! WebView2 runtime — no bundled Chromium, no second V8, ~an order of
//! magnitude less RAM — and the browser flags below keep compositing and
//! rasterization on the GPU even on old (DirectX 10-class) graphics cards.

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::Mutex;
use std::time::{Duration, Instant};

use tauri::{Manager, WebviewUrl, WebviewWindowBuilder};
use tauri_plugin_dialog::DialogExt;

/// Transparent, always-on-top overlay window carrying the styled stream
/// chrome (deck, live stats, toasts) above the native video plane.
mod native_overlay;
/// Main-window handle breadcrumb shared with the backend (see
/// `publish_window_handle`). Lives in the app data directory, next to
/// `server.log`.
const WINDOW_HANDLE_FILE: &str = "window-handle.json";

/// WebView2 command-line switches for the main window.
///
/// `--ignore-gpu-blocklist` is the critical one for DirectX 10-class GPUs:
/// Chromium normally falls back to software rendering (SwiftShader) once a
/// GPU or driver lands on its blocklist, which makes a blur-heavy UI crawl.
/// The flag keeps the GPU pipeline active — D3D11 feature level 10_0, which
/// every DirectX 10 GPU supports, is all ANGLE needs for compositing,
/// rasterization, and WebGL.
///
/// The `msWebOOUI/msPdfOOUI/msSmartScreenProtection` disables are the default
/// switches wry applies; supplying any additional args replaces them, so they
/// are restated here.
const BROWSER_ARGS: &str = concat!(
    "--disable-features=msWebOOUI,msPdfOOUI,msSmartScreenProtection ",
    "--ignore-gpu-blocklist ",
    "--enable-gpu-rasterization ",
    "--autoplay-policy=no-user-gesture-required ",
    "--disable-background-timer-throttling ",
    "--disable-renderer-backgrounding"
);

/// Runs before the client boots: routes external links to the system browser
/// through the opener plugin. The client never navigates away from its own
/// origin, but the NVIDIA device-login flow and store pages must open in a
/// real browser.
const EXTERNAL_LINK_SCRIPT: &str = r#"(function () {
  if (!window.__TAURI__ || !window.__TAURI__.opener) return;
  var opener = window.__TAURI__.opener;
  function resolveExternal(value) {
    try {
      var url = new URL(String(value), window.location.href);
      if ((url.protocol === "http:" || url.protocol === "https:") && url.origin !== window.location.origin) {
        return url.toString();
      }
    } catch (error) { /* not a URL — fall through */ }
    return null;
  }
  var nativeOpen = window.open.bind(window);
  window.open = function (url, target, features) {
    var external = resolveExternal(url);
    if (external !== null) {
      opener.openUrl(external);
      return null;
    }
    return nativeOpen(url, target, features);
  };
  document.addEventListener(
    "click",
    function (event) {
      var node = event.target;
      var anchor = node && node.closest ? node.closest('a[target="_blank"]') : null;
      if (!anchor) return;
      var external = resolveExternal(anchor.getAttribute("href"));
      if (external === null) return;
      event.preventDefault();
      opener.openUrl(external);
    },
    true
  );
})();
"#;

/// Handle to the bundled backend process so it can be terminated on exit.
struct BackendProcess {
    child: Mutex<Option<Child>>,
}

#[tauri::command]
fn get_window_handle(window: tauri::Window) -> Result<String, String> {
    native_window_handle(&window)
}

/// Real-window fullscreen for the in-app player.
///
/// The native (NVST) video plane is a child window positioned over this
/// window's client area, so a DOM fullscreen request alone would only fill the
/// WebView — the OS window (and therefore the video surface) would stay
/// windowed. F11 in the stream, the deck's fullscreen button and the engine's
/// `toggle-fullscreen` shortcut all end up here.
#[tauri::command]
fn set_app_fullscreen(window: tauri::Window, fullscreen: bool) -> Result<(), String> {
    window
        .set_fullscreen(fullscreen)
        .map_err(|error| error.to_string())
}

/// Win32 handle of a shell window as a decimal string, `"0"` elsewhere.
fn native_window_handle<H>(window: &H) -> Result<String, String>
where
    H: raw_window_handle::HasWindowHandle,
{
    #[cfg(target_os = "windows")]
    {
        use raw_window_handle::RawWindowHandle;
        let handle = window.window_handle().map_err(|e| e.to_string())?;
        match handle.as_raw() {
            RawWindowHandle::Win32(h) => Ok(format!("{}", h.hwnd.get() as usize)),
            _ => Err("Not a Win32 window".into()),
        }
    }
    #[cfg(not(target_os = "windows"))]
    {
        let _ = window;
        Ok("0".into())
    }
}

/// Breadcrumb the backend reads when the web client asks for the main window's
/// handle. The Tauri IPC bridge (`get_window_handle`) is the fast path, but a
/// page loaded from the loopback backend origin is a *remote* origin for Tauri,
/// so IPC can be unavailable — without this file the native engine would never
/// be told where to embed and would fall back to a separate window.
fn publish_window_handle(app: &tauri::AppHandle) {
    let Some(window) = app.get_webview_window("main") else {
        eprintln!("[OpenNOW] main window missing — native surface embedding has no handle");
        return;
    };
    let handle = match native_window_handle(&window) {
        Ok(handle) if handle != "0" => handle,
        Ok(_) => return,
        Err(error) => {
            eprintln!("[OpenNOW] could not resolve the main window handle: {error}");
            return;
        }
    };
    let Ok(data_dir) = app.path().app_data_dir() else {
        return;
    };
    let written_at_ms = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|elapsed| elapsed.as_millis())
        .unwrap_or_default();
    let payload = format!(
        "{{\"handle\":\"{handle}\",\"pid\":{},\"writtenAtMs\":{written_at_ms}}}\n",
        std::process::id()
    );
    if let Err(error) = std::fs::write(simplified(&data_dir).join(WINDOW_HANDLE_FILE), payload) {
        eprintln!("[OpenNOW] could not publish the main window handle: {error}");
    }
}

/// The handle file is only meaningful while the shell that wrote it is alive.
fn remove_window_handle_file(app: &tauri::AppHandle) {
    if let Ok(data_dir) = app.path().app_data_dir() {
        let _ = std::fs::remove_file(simplified(&data_dir).join(WINDOW_HANDLE_FILE));
    }
}

fn main() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            get_window_handle,
            set_app_fullscreen,
            native_overlay::native_overlay_command,
            native_overlay::native_overlay_state,
            native_overlay::native_overlay_action,
            native_overlay::native_overlay_push,
            native_overlay::native_overlay_ready,
            native_overlay::native_overlay_hide,
        ])
        // Must stay the first registered plugin.
        .plugin(tauri_plugin_single_instance::init(|app, _args, _cwd| {
            if let Some(window) = app.get_webview_window("main") {
                let _ = window.unminimize();
                let _ = window.set_focus();
            }
        }))
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_dialog::init())
        .setup(|app| {
            let handle = app.handle().clone();
            // Backend boot + window creation happen off the main thread so the
            // event loop stays responsive while we poll /api/health.
            std::thread::spawn(move || {
                if let Err(error) = startup(&handle) {
                    eprintln!("[OpenNOW] startup failed: {error}");
                    show_startup_error(&handle, &error.to_string());
                    handle.exit(1);
                }
            });
            Ok(())
        })
        .build(tauri::generate_context!())
        .expect("error while building the OpenNOW desktop shell")
        .run(|app_handle, event| {
            if let tauri::RunEvent::Exit = event {
                remove_window_handle_file(app_handle);
                terminate_backend(app_handle);
            }
        });
}

fn startup(app: &tauri::AppHandle) -> Result<(), Box<dyn std::error::Error>> {
    #[cfg(debug_assertions)]
    let origin = {
        // `tauri dev` runs `npm run dev` (beforeDevCommand) and the CLI waits
        // for devUrl (localhost:3000) before launching this binary, so the
        // dev server is already up — no sidecar in development.
        "http://localhost:3000".to_string()
    };

    #[cfg(not(debug_assertions))]
    let origin = start_backend(app)?;

    open_main_window(app, &origin)?;
    publish_window_handle(app);

    // The overlay window is intentionally created even when the user never
    // opens the deck: it boots once (a few MB, no rendering while hidden) and
    // is then instantly available for Ctrl+G / Ctrl+N / guide-button requests
    // coming from the native engine. A failure here is not fatal — WebRTC and
    // native playback both keep working.
    if let Err(error) = native_overlay::setup(app, &origin) {
        eprintln!("[OpenNOW] styled stream overlay unavailable, engine menu stays in use: {error}");
    }

    Ok(())
}

/// Spawns the bundled backend and returns the origin to load in the window.
#[cfg(not(debug_assertions))]
fn start_backend(app: &tauri::AppHandle) -> Result<String, Box<dyn std::error::Error>> {
    // Tauri's path APIs (and `std::env::current_exe`) return `\\?\`-prefixed
    // verbatim paths on Windows. Native APIs accept them, but Node's module
    // loader cannot resolve the entry script through one (it dies with
    // `EISDIR: lstat 'C:'`), so normalize every directory before use.
    let resource_dir = simplified(&app.path().resource_dir()?);
    let exe_dir_raw = std::env::current_exe()?
        .parent()
        .map(Path::to_path_buf)
        .ok_or("could not resolve the application directory")?;
    let exe_dir = simplified(&exe_dir_raw);

    let triple = env!("OPENNOW_TARGET_TRIPLE");
    // Tauri strips the `-<triple>` suffix from `externalBin` sidecars when
    // bundling (the suffix is only the build-time lookup convention for
    // `src-tauri/binaries/`), so an installed app ships `opennow-server.exe`
    // while the CI portable ZIP keeps the suffixed name — accept both.
    let server_exe = first_existing_file(&[
        resource_dir.join("opennow-server.exe"),
        resource_dir.join("opennow-server"),
        exe_dir.join("opennow-server.exe"),
        exe_dir.join("opennow-server"),
        resource_dir.join(format!("opennow-server-{triple}.exe")),
        resource_dir.join(format!("opennow-server-{triple}")),
        exe_dir.join(format!("opennow-server-{triple}.exe")),
        exe_dir.join(format!("opennow-server-{triple}")),
    ])
    .ok_or("bundled backend executable (opennow-server) not found — the installation may be incomplete, try reinstalling OpenNOW")?;
    let server_js =
        first_existing_file(&[resource_dir.join("server.mjs"), exe_dir.join("server.mjs")])
            .ok_or("bundled backend bundle (server.mjs) not found — the installation may be incomplete, try reinstalling OpenNOW")?;
    // `dist/` is a directory, so it needs the `is_dir` predicate — `is_file`
    // is false for directories and this lookup used to fail unconditionally.
    let static_dir = first_existing_dir(&[resource_dir.join("dist"), exe_dir.join("dist")])
        .ok_or("bundled web client (dist) not found — the installation may be incomplete, try reinstalling OpenNOW")?;
    // Native (NVST) sidecar: the upstream streaming engine bundled as an
    // `externalBin`. Same stripped/suffixed lookup as the backend, but
    // strictly optional — WebRTC playback works fine without it.
    let nvst_exe = first_existing_file(&[
        resource_dir.join("opennow-nvst.exe"),
        resource_dir.join("opennow-nvst"),
        exe_dir.join("opennow-nvst.exe"),
        exe_dir.join("opennow-nvst"),
        resource_dir.join(format!("opennow-nvst-{triple}.exe")),
        resource_dir.join(format!("opennow-nvst-{triple}")),
        exe_dir.join(format!("opennow-nvst-{triple}.exe")),
        exe_dir.join(format!("opennow-nvst-{triple}")),
    ]);

    let data_dir = simplified(&app.path().app_data_dir()?);
    std::fs::create_dir_all(&data_dir)?;
    // Launcher-side breadcrumb for support: path resolution is invisible
    // otherwise (release builds have no console). Best-effort by design.
    let _ = std::fs::write(
        data_dir.join("launcher.log"),
        format!(
            "resource_dir={}\nexe_dir={}\nserver_exe={}\nserver_js={}\nstatic_dir={}\nnvst_exe={}\n",
            resource_dir.display(),
            exe_dir.display(),
            server_exe.display(),
            server_js.display(),
            static_dir.display(),
            nvst_exe
                .as_ref()
                .map(|path| path.display().to_string())
                .unwrap_or_else(|| "(not bundled)".to_string())
        ),
    );
    let log_file = std::fs::File::create(data_dir.join("server.log"))?;
    let log_file_stderr = log_file.try_clone()?;

    let port = pick_desktop_port();
    let origin = format!("http://127.0.0.1:{port}");

    let child = Command::new(&server_exe)
        .arg(&server_js)
        .current_dir(&data_dir)
        .env("NODE_ENV", "production")
        .env("PORT", port.to_string())
        // Loopback-only: the backend must not listen on every interface the
        // way the deployment server does, and binding 127.0.0.1 also avoids
        // Windows Firewall prompts on first launch.
        .env("OPENNOW_BIND_HOST", "127.0.0.1")
        .env("OPENNOW_ORIGIN", &origin)
        .env("OPENNOW_STATIC_DIR", &static_dir)
        .env("OPENNOW_DATA_DIR", &data_dir)
        .env("OPENNOW_SESSION_SECRET_FILE", data_dir.join("session-secret"))
        // The backend serves plain HTTP on loopback, which can never satisfy
        // the Secure cookie flag — without this override the login session
        // cookie is dropped and the user must log in on every launch.
        .env("OPENNOW_COOKIE_SECURE", "0")
        .env("OPENNOW_PARENT_PID", std::process::id().to_string())
        // Empty when the engine was not bundled; the backend treats that as
        // "native playback unavailable" and WebRTC is unaffected.
        .env(
            "OPENNOW_NVST_SIDECAR",
            nvst_exe.as_deref().unwrap_or_else(|| Path::new("")),
        )
        .stdout(Stdio::from(log_file))
        .stderr(Stdio::from(log_file_stderr))
        .spawn()?;
    eprintln!("[OpenNOW] backend spawned on {origin}");
    app.manage(BackendProcess {
        child: Mutex::new(Some(child)),
    });

    // Wait until the backend answers /api/health. Cold starts (first-launch
    // cache initialization, antivirus scanning the fresh bundle) can take a
    // few seconds on older hardware, so the budget is generous.
    let deadline = Instant::now() + Duration::from_secs(45);
    loop {
        if http_get_ok(port, "/api/health") {
            return Ok(origin);
        }
        if backend_exited(app) {
            return Err(
                "the bundled backend exited during startup.\n\nDetails: server.log inside the OpenNOW app data folder."
                    .into(),
            );
        }
        if Instant::now() > deadline {
            return Err(
                "timed out waiting for the bundled backend.\n\nDetails: server.log inside the OpenNOW app data folder."
                    .into(),
            );
        }
        std::thread::sleep(Duration::from_millis(150));
    }
}

fn open_main_window(
    app: &tauri::AppHandle,
    origin: &str,
) -> Result<(), Box<dyn std::error::Error>> {
    let url: tauri::Url = origin.parse()?;
    // In-app native stream: SDL child window is embedded inside Tauri window via Win32 SetParent.
    // WebView background must be transparent where video hole is, so native surface shows through
    // (no black WebRTC screen). Use transparent window + transparent WebView.
    // Black background in CSS will still paint, but hole area (opacity 0.01) lets SDL child show through.
    let mut builder = WebviewWindowBuilder::new(app, "main", WebviewUrl::External(url))
        .title("OpenNOW")
        .inner_size(1280.0, 800.0)
        .min_inner_size(1000.0, 640.0)
        .resizable(true)
        // Native NVST video is clipped into this shell window. Start the host
        // maximized so the first surface rect covers the available screen
        // instead of leaving the embedded stream in a centered 1280x800 window.
        .maximized(true)
        .transparent(true)
        .background_color(tauri::window::Color(0, 0, 0, 0))
        .initialization_script(EXTERNAL_LINK_SCRIPT);

    #[cfg(target_os = "windows")]
    {
        builder = builder
            .theme(Some(tauri::Theme::Dark))
            .additional_browser_args(BROWSER_ARGS);
    }

    builder.build()?;
    Ok(())
}

/// Native error dialog for fatal startup failures (backend missing, crashed,
/// or never became healthy). Without this the app would just silently exit.
/// The message is shown verbatim: callers append the server.log pointer only
/// when the backend was actually spawned (missing-file errors have no log).
fn show_startup_error(app: &tauri::AppHandle, message: &str) {
    let _ = app
        .dialog()
        .message(message.to_string())
        .title("OpenNOW failed to start")
        .kind(tauri_plugin_dialog::MessageDialogKind::Error)
        .blocking_show();
}

fn terminate_backend(app: &tauri::AppHandle) {
    if let Some(state) = app.try_state::<BackendProcess>() {
        if let Ok(mut guard) = state.child.lock() {
            if let Some(child) = guard.as_mut() {
                let _ = child.kill();
                let _ = child.wait();
            }
            *guard = None;
        }
    }
}

#[cfg(not(debug_assertions))]
fn backend_exited(app: &tauri::AppHandle) -> bool {
    app.try_state::<BackendProcess>()
        .and_then(|state| {
            let mut guard = state.child.lock().ok()?;
            let child = guard.as_mut()?;
            Some(matches!(child.try_wait(), Ok(Some(_))))
        })
        .unwrap_or(false)
}

fn first_existing_file(candidates: &[PathBuf]) -> Option<PathBuf> {
    candidates.iter().find(|path| path.is_file()).cloned()
}

fn first_existing_dir(candidates: &[PathBuf]) -> Option<PathBuf> {
    candidates.iter().find(|path| path.is_dir()).cloned()
}

/// Strips Windows verbatim (`\\?\`) prefixes: `\\?\C:\...` becomes `C:\...`
/// and `\\?\UNC\server\share` becomes `\\server\share`. Returns the input
/// untouched when no prefix is present (including on other platforms).
fn simplified(path: &Path) -> PathBuf {
    const VERBATIM: &str = r"\\?\";
    const UNC: &str = r"\\?\UNC\";
    let text = path.to_string_lossy();
    if let Some(rest) = text.strip_prefix(UNC) {
        PathBuf::from(format!(r"\\{rest}"))
    } else if let Some(rest) = text.strip_prefix(VERBATIM) {
        PathBuf::from(rest)
    } else {
        path.to_path_buf()
    }
}

/// Preferred loopback port for the bundled backend. The port is part of the
/// WebView origin, so a random port every launch partitions localStorage per
/// launch and silently resets all app settings — a stable port keeps the
/// origin (and the stored settings) identical across restarts. Deliberately
/// outside the GameStream/Moonlight/Sunshine range (47984-48010).
const DESKTOP_PORT_DEFAULT: u16 = 48127;

/// Binds :0 on loopback, reads the assigned port, and drops the listener.
/// The backend binds it a moment later — the tiny race is the standard
/// trade-off for dynamic port picking and is harmless (worst case the health
/// poll fails and the error is reported).
fn pick_free_loopback_port() -> Result<u16, Box<dyn std::error::Error>> {
    let listener = TcpListener::bind(("127.0.0.1", 0))?;
    Ok(listener.local_addr()?.port())
}

/// Stable desktop port with a random fallback. The single-instance guard
/// makes a conflict unlikely (only a stale crashed backend could hold the
/// port); on conflict the app still starts, but settings edited in that run
/// land on a different origin and will not persist.
fn pick_desktop_port() -> u16 {
    let preferred: u16 = std::env::var("OPENNOW_DESKTOP_PORT")
        .ok()
        .and_then(|value| value.trim().parse().ok())
        .unwrap_or(DESKTOP_PORT_DEFAULT);
    if preferred != 0 && TcpListener::bind(("127.0.0.1", preferred)).is_ok() {
        return preferred;
    }
    eprintln!(
        "[OpenNOW] preferred loopback port {preferred} is unavailable — falling back to a random port (settings changed this run will not persist)"
    );
    pick_free_loopback_port().unwrap_or(DESKTOP_PORT_DEFAULT)
}

/// Minimal HTTP/1.0 HEAD-ish check without pulling in an HTTP client crate:
/// connect, GET the path, and look for a 200 status line.
fn http_get_ok(port: u16, path: &str) -> bool {
    let Ok(mut stream) = TcpStream::connect(("127.0.0.1", port)) else {
        return false;
    };
    let _ = stream.set_read_timeout(Some(Duration::from_secs(2)));
    let request = format!("GET {path} HTTP/1.0\r\nHost: 127.0.0.1:{port}\r\nConnection: close\r\n\r\n");
    if stream.write_all(request.as_bytes()).is_err() {
        return false;
    }
    let mut head = [0u8; 128];
    let Ok(read) = stream.read(&mut head) else {
        return false;
    };
    let response = String::from_utf8_lossy(&head[..read]);
    response.starts_with("HTTP/1.1 200") || response.starts_with("HTTP/1.0 200")
}
