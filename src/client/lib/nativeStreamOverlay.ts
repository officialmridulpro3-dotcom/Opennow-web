import type { StreamDiagnostics } from "../platforms/gfn/webrtcClient";

export const NATIVE_STREAM_OVERLAY_CHANNEL = "opennow:native-stream-overlay:v1";

export type NativeStreamOverlayAction =
  | "ready"
  | "toggle-menu"
  | "close-menu"
  | "toggle-stats"
  | "toggle-fullscreen"
  | "toggle-pointer-lock"
  | "toggle-microphone"
  | "toggle-recording"
  | "toggle-anti-afk"
  | "prompt-stop-stream"
  | "end-stream";

export interface NativeStreamOverlayState {
  menuOpen: boolean;
  statsVisible: boolean;
  gameTitle: string;
  gameCover?: string | null;
  gameHero?: string | null;
  platformName?: string;
  sessionStartedAtMs: number | null;
  sessionTimeRemainingSeconds: number | null;
  serverRegion?: string;
  diagnostics: StreamDiagnostics;
  isFullscreen: boolean;
  inputCaptured: boolean;
  antiAfkEnabled: boolean;
  microphoneAvailable: boolean;
  microphoneEnabled: boolean;
  recording: boolean;
  shortcuts: {
    toggleStats: string;
    togglePointerLock: string;
    toggleFullscreen: string;
    stopStream: string;
    toggleAntiAfk: string;
    toggleMicrophone: string;
    screenshot: string;
    recording: string;
    toggleSidebar: string;
  };
}

export type NativeStreamOverlayMessage =
  | { type: "ready" }
  | { type: "state"; state: NativeStreamOverlayState }
  | { type: "prompt-end-session" }
  | { type: "stats"; diagnostics: StreamDiagnostics }
  | { type: "microphone-state"; enabled: boolean; state?: string; message?: string }
  | { type: "command"; action: Exclude<NativeStreamOverlayAction, "ready"> };

type OverlayListener = (message: NativeStreamOverlayMessage) => void;
type TauriWindow = Window & {
  __TAURI__?: {
    core?: {
      invoke?: <T>(command: string, args?: Record<string, unknown>) => Promise<T>;
    };
  };
};

const listeners = new Set<OverlayListener>();
const channel: BroadcastChannel | null = typeof BroadcastChannel === "undefined"
  ? null
  : new BroadcastChannel(NATIVE_STREAM_OVERLAY_CHANNEL);

if (channel) {
  channel.addEventListener("message", (event: MessageEvent<NativeStreamOverlayMessage>) => {
    for (const listener of listeners) {
      listener(event.data);
    }
  });
}

export function isTauriDesktop(): boolean {
  return typeof window !== "undefined" && typeof (window as TauriWindow).__TAURI__?.core?.invoke === "function";
}

export function subscribeNativeStreamOverlay(listener: OverlayListener): () => void {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

export function postNativeStreamOverlayMessage(message: NativeStreamOverlayMessage): void {
  channel?.postMessage(message);
}

export function sendNativeStreamOverlayCommand(action: Exclude<NativeStreamOverlayAction, "ready">): void {
  postNativeStreamOverlayMessage({ type: "command", action });
}

/**
 * Show/hide the transparent Tauri overlay and keep its bounds aligned with the
 * app window. The overlay is click-through unless a menu is open.
 */
export async function setNativeStreamOverlayWindow(
  visible: boolean,
  interactive: boolean,
  url: string,
): Promise<void> {
  const invoke = (window as TauriWindow).__TAURI__?.core?.invoke;
  if (!invoke) return;
  await invoke("set_native_stream_overlay", { visible, interactive, url });
}
