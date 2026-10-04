import type {
  NativeOverlayCommand,
  NativeOverlayMessage,
  NativeOverlayStats,
  NativeOverlaySession,
  NativeOverlayToggles,
} from "./components/native-overlay/types";

/**
 * Main-window side of the native stream overlay bridge.
 *
 * The native (NVST) engine presents gameplay in a D3D11 child window that sits
 * *above* the main WebView, so the stream chrome cannot be part of this page.
 * The Tauri shell floats a second, transparent web view over the video plane
 * (`src-tauri/src/native_overlay.rs`); this module is the pipe between the two:
 *
 *   • here  → overlay   `native_overlay_command` / `native_overlay_push`
 *   • overlay → here    `opennow:native-overlay-action`
 *
 * Every function degrades to a no-op in the browser build, so WebRTC playback
 * and the plain web client are unaffected.
 */

const DATA_EVENT = "opennow:native-overlay-data";
const ACTION_EVENT = "opennow:native-overlay-action";

interface TauriGlobal {
  core?: { invoke?: (command: string, args?: Record<string, unknown>) => Promise<unknown> };
  event?: {
    listen?: (
      event: string,
      handler: (event: { payload: unknown }) => void,
    ) => Promise<() => void>;
  };
}

function tauri(): TauriGlobal | undefined {
  return (window as unknown as { __TAURI__?: TauriGlobal }).__TAURI__;
}

function invoke(command: string, args?: Record<string, unknown>): void {
  const call = tauri()?.core?.invoke?.bind(tauri()?.core);
  if (!call) return;
  void call(command, args).catch((error: unknown) => {
    console.warn(`[Overlay] ${command} failed:`, error);
  });
}

/** True inside the Tauri shell, where the overlay window exists. */
export function isNativeOverlayHostAvailable(): boolean {
  return Boolean(tauri()?.core?.invoke);
}

/** Ask the overlay window to change what it shows. */
export function sendNativeOverlayCommand(command: NativeOverlayCommand): void {
  if (!isNativeOverlayHostAvailable()) return;
  invoke("native_overlay_command", { command });
}

/** Push session info, stats, toggles or a toast into the overlay. */
export function pushNativeOverlayMessage(message: NativeOverlayMessage): void {
  if (!isNativeOverlayHostAvailable()) return;
  invoke("native_overlay_push", { message });
}

export function pushNativeOverlaySession(session: Partial<NativeOverlaySession>): void {
  pushNativeOverlayMessage({ kind: "session", session: session as NativeOverlaySession });
}

export function pushNativeOverlayStats(stats: NativeOverlayStats): void {
  pushNativeOverlayMessage({ kind: "stats", stats });
}

export function pushNativeOverlayToggles(toggles: Partial<NativeOverlayToggles>): void {
  pushNativeOverlayMessage({ kind: "toggles", toggles });
}

export function pushNativeOverlayToast(
  toast: { kind: "info" | "success" | "warning"; title: string; detail?: string },
): void {
  pushNativeOverlayMessage({ kind: "toast", toast });
}

/** Session over: hide the overlay window and hand input back to the game. */
export function hideNativeOverlay(): void {
  if (!isNativeOverlayHostAvailable()) return;
  invoke("native_overlay_hide");
}

/**
 * Listens for deck/HUD actions. Returns an unsubscribe function; the listener
 * receives the action name and an optional numeric value (sliders).
 */
export function subscribeNativeOverlayActions(
  listener: (action: string, value?: number) => void,
): () => void {
  const listen = tauri()?.event?.listen?.bind(tauri()?.event);
  if (!listen) return () => {};
  const pending = listen(ACTION_EVENT, (event) => {
    const payload = event.payload as { action?: unknown; value?: unknown } | undefined;
    const action = typeof payload?.action === "string" ? payload.action : "";
    if (!action) return;
    listener(action, typeof payload?.value === "number" ? payload.value : undefined);
  });
  return () => {
    void pending.then((off) => off()).catch(() => {});
  };
}

export const NATIVE_OVERLAY_EVENTS = { DATA_EVENT, ACTION_EVENT } as const;
