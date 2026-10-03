import { existsSync, mkdirSync, readFileSync, renameSync, rmSync, writeFileSync } from "node:fs";
import { homedir } from "node:os";
import { dirname, join, resolve } from "node:path";

/**
 * Per-installation writable data directory.
 *
 * Everything OpenNOW must remember between launches lives here: the session
 * encryption secret, the desktop sign-in mirror, and the playtime ledger.
 *
 * The desktop shell spawns the backend with `OPENNOW_DATA_DIR` pointed at
 * Tauri's app-data folder and `cwd` set to the same directory, so the value is
 * stable across launches, updates, and reboots. Plain deployments fall back to
 * the conventional per-platform location.
 *
 * Resolution is deliberately lazy (called per access, never cached at module
 * load) so tests can repoint it through the environment.
 */
export function resolveAppDataDir(): string {
  const override = process.env.OPENNOW_DATA_DIR?.trim();
  if (override) return resolve(override);

  const platform = process.platform;
  if (platform === "win32") {
    const base = process.env.LOCALAPPDATA?.trim() || join(homedir(), "AppData", "Local");
    return resolve(join(base, "OpenNOW"));
  }
  if (platform === "darwin") {
    return resolve(join(homedir(), "Library", "Application Support", "OpenNOW"));
  }
  const base = process.env.XDG_DATA_HOME?.trim() || join(homedir(), ".local", "share");
  return resolve(join(base, "opennow"));
}

/**
 * Whether this backend is the single-user loopback server bundled with the
 * desktop app.
 *
 * The distinction matters for on-disk session persistence: a hosted deployment
 * serves many visitors from one process, so writing "the" session to disk would
 * leak one visitor's tokens into the next visitor's request. The desktop build
 * has exactly one user, which is what makes a sign-in mirror safe (and the only
 * way to survive a WebView that drops its cookie jar).
 */
export function isSingleUserDesktopRuntime(): boolean {
  if (process.env.OPENNOW_DATA_DIR?.trim()) return true;
  if (process.env.OPENNOW_PARENT_PID?.trim()) return true;
  return process.env.OPENNOW_BIND_HOST?.trim() === "127.0.0.1";
}

export function appDataPath(fileName: string): string {
  return join(resolveAppDataDir(), fileName);
}

/** Creates the data directory (and parents) on demand. Returns null on failure. */
export function ensureAppDataDir(): string | null {
  const dir = resolveAppDataDir();
  try {
    mkdirSync(dir, { recursive: true, mode: 0o700 });
    return dir;
  } catch (error) {
    console.warn(`[AppData] Could not create ${dir}: ${String(error)}`);
    return null;
  }
}

export function readAppDataFile(fileName: string): string | null {
  try {
    return readFileSync(appDataPath(fileName), "utf8");
  } catch {
    return null;
  }
}

/**
 * Atomic write: land the payload in a sibling temp file, then rename over the
 * target. A crash mid-write leaves the previous contents intact instead of a
 * truncated ledger. Returns false when the directory is not writable.
 */
export function writeAppDataFile(fileName: string, contents: string, mode = 0o600): boolean {
  const target = appDataPath(fileName);
  if (!ensureAppDataDir()) return false;
  const temporary = `${target}.${process.pid}.tmp`;
  try {
    writeFileSync(temporary, contents, { mode });
    renameSync(temporary, target);
    return true;
  } catch (error) {
    console.warn(`[AppData] Could not write ${target}: ${String(error)}`);
    try {
      rmSync(temporary, { force: true });
    } catch {
      // Best effort.
    }
    return false;
  }
}

export function removeAppDataFile(fileName: string): void {
  try {
    rmSync(appDataPath(fileName), { force: true });
  } catch {
    // Absent file is the desired end state anyway.
  }
}

export function appDataFileExists(fileName: string): boolean {
  try {
    return existsSync(appDataPath(fileName));
  } catch {
    return false;
  }
}

/** Directory that holds a relative path's parent, created on demand. */
export function ensureParentDir(filePath: string): void {
  try {
    mkdirSync(dirname(resolve(filePath)), { recursive: true });
  } catch {
    // Callers treat the write itself as best-effort.
  }
}
