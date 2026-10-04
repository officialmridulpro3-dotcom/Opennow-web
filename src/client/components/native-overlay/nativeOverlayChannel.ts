import type {
  NativeOverlayAction,
  NativeOverlayChannel,
  NativeOverlayMessage,
} from "./types";

/**
 * Tauri host bridge for the transparent overlay window.
 *
 * Talks to `src-tauri/src/native_overlay.rs`:
 *   • in  — `opennow:native-overlay-data`  (session, stats, toggles, toasts, commands)
 *   • out — `native_overlay_action` / `native_overlay_state`
 *
 * The overlay window is created and positioned by the Rust shell; the web layer
 * only describes what it wants to show. Everything degrades to a no-op outside
 * the desktop app, so the same bundle runs in a plain browser.
 */

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

export function isNativeOverlayBridgeAvailable(): boolean {
  const api = tauri();
  return Boolean(api?.core?.invoke && api?.event?.listen);
}

export function createTauriOverlayChannel(): NativeOverlayChannel {
  const invoke = tauri()?.core?.invoke?.bind(tauri()?.core) as
    | ((command: string, args?: Record<string, unknown>) => Promise<unknown>)
    | undefined;

  return {
    subscribe(listener) {
      const listen = tauri()?.event?.listen?.bind(tauri()?.event);
      if (!listen) return () => {};
      const unlisten = listen(OPENNOW_OVERLAY_DATA_EVENT, (event) => {
        const payload = event.payload as NativeOverlayMessage | undefined;
        if (payload && typeof payload === "object" && "kind" in payload) listener(payload);
      });
      return () => {
        void unlisten.then((off) => off()).catch(() => {});
      };
    },
    dispatch(action: NativeOverlayAction, value?: number) {
      void invoke?.("native_overlay_action", { action, value }).catch(() => {});
    },
    reportVisibility(state) {
      void invoke?.("native_overlay_state", {
        deckOpen: state.deckOpen,
        hudVisible: state.hudVisible,
      }).catch(() => {});
    },
    signalReady() {
      void invoke?.("native_overlay_ready").catch(() => {});
    },
  };
}

export const OPENNOW_OVERLAY_DATA_EVENT = "opennow:native-overlay-data";
export const OPENNOW_OVERLAY_ACTION_EVENT = "opennow:native-overlay-action";
