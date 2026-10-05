import { readAppDataFile } from "./appData";

/**
 * Main-window handle published by the desktop shell.
 *
 * The shell writes `<app-data>/window-handle.json` right after it opens the
 * main window (see `publish_window_handle` in `src-tauri/src/main.rs`). The
 * Tauri IPC bridge (`get_window_handle`) stays the primary path — it is a
 * direct call with no file round-trip — but the web client is served from the
 * loopback backend origin, which Tauri treats as a *remote* origin. When the
 * capability that grants that origin IPC is missing (or a future Tauri release
 * changes the default), `get_window_handle` silently never resolves and the
 * native engine would fall back to a window of its own. This breadcrumb keeps
 * the embedded-surface handshake working without IPC.
 */
const WINDOW_HANDLE_FILE = "window-handle.json";

/**
 * A handle file older than this is treated as stale: the shell rewrites it on
 * every launch, so an old timestamp means no shell is running (a plain browser
 * session pointed at the desktop data directory).
 */
const MAX_AGE_MS = 12 * 60 * 60 * 1000;

export interface ShellWindowHandle {
  handle: string;
  pid: number;
  writtenAtMs: number;
}

/** `process.kill(pid, 0)` is the only portable liveness probe Node ships. */
function isProcessAlive(pid: number): boolean {
  try {
    process.kill(pid, 0);
    return true;
  } catch (error) {
    // A process owned by another user exists but cannot be signalled; that is
    // still alive for our purposes, so keep the handle.
    return (error as NodeJS.ErrnoException).code === "EPERM";
  }
}

/**
 * Reads the shell's main-window handle, or null when there is none to trust.
 *
 * Rejects anything that is not a non-zero decimal string, anything written by
 * a dead process, and anything older than `MAX_AGE_MS` — a handle pointing at
 * a recycled HWND would make the engine attach its surface to a foreign
 * window.
 */
export function readShellWindowHandle(now: number = Date.now()): ShellWindowHandle | null {
  const raw = readAppDataFile(WINDOW_HANDLE_FILE);
  if (!raw) return null;

  let parsed: unknown;
  try {
    parsed = JSON.parse(raw);
  } catch {
    return null;
  }
  if (typeof parsed !== "object" || parsed === null) return null;

  const record = parsed as Partial<Record<keyof ShellWindowHandle, unknown>>;
  const handle = typeof record.handle === "string" ? record.handle.trim() : "";
  if (!/^[1-9][0-9]*$/.test(handle)) return null;

  const pid = typeof record.pid === "number" && Number.isInteger(record.pid) ? record.pid : 0;
  const writtenAtMs =
    typeof record.writtenAtMs === "number" && Number.isFinite(record.writtenAtMs)
      ? record.writtenAtMs
      : 0;
  if (writtenAtMs <= 0 || now - writtenAtMs > MAX_AGE_MS || writtenAtMs > now + 60_000) {
    return null;
  }
  if (pid > 0 && !isProcessAlive(pid)) return null;

  return { handle, pid, writtenAtMs };
}
