/**
 * Native stream overlay — shared model.
 *
 * The overlay is a separate, transparent web view that floats above the native
 * (NVST) video window. It is deliberately data-driven: the host (main window /
 * Tauri shell) pushes {@link NativeOverlayMessage}s in, the overlay pushes
 * {@link NativeOverlayAction}s out, so the same components can run against the
 * real engine, the preview harness, or a test double.
 */

export type NativeOverlayTab = "session" | "controls" | "media" | "keys";

export type NativeOverlayCommand =
  | "toggle-deck"
  | "open-deck"
  | "close-deck"
  | "toggle-hud"
  | "hud-on"
  | "hud-off";

export interface NativeOverlaySession {
  gameTitle: string;
  /** Key art / hero image; the deck degrades to a gradient when absent. */
  heroImageUrl?: string | null;
  coverImageUrl?: string | null;
  store?: string | null;
  region?: string | null;
  /** Epoch ms when the seat started streaming; drives the session timer. */
  startedAtMs?: number | null;
  transport?: "nvst" | "webrtc";
  enginePid?: number | null;
  protocolVersion?: number | null;
  firstFrame?: boolean;
  recording?: { path: string; startedAtMs: number } | null;
  /** Remaining session time in seconds, when the seat reports one. */
  remainingSeconds?: number | null;
}

export interface NativeOverlayStats {
  codec?: string;
  resolution?: string;
  /**
   * Stream and decode rates. `decodedFps` is what the decoder produced this
   * second; `renderFps` is what actually reached the swap chain.
   */
  decodedFps?: number;
  renderFps?: number;
  bitrateKbps?: number;
  targetBitrateKbps?: number;
  rttMs?: number;
  packetLossPercent?: number;
  framesDecoded?: number;
  framesDropped?: number;
  hardwareAcceleration?: string;
  queueMode?: string;
  zeroCopy?: boolean;
  updatedAtMs?: number;
}

export interface NativeOverlayToggles {
  fullscreen: boolean;
  micMuted: boolean;
  recording: boolean;
  inputCaptured: boolean;
  hudVisible: boolean;
  /** Mouse sensitivity bias in percent (100 = the stream setting as-is). */
  mouseSensitivity: number;
}

export interface NativeOverlayToast {
  id: string;
  kind: "info" | "success" | "warning";
  title: string;
  detail?: string;
  /** Auto-dismiss delay; 0 keeps the toast until dismissed. */
  ttlMs?: number;
}

export type NativeOverlayAction =
  | "resume"
  | "end-session"
  | "toggle-fullscreen"
  | "capture-mouse"
  | "toggle-mic"
  | "screenshot"
  | "toggle-recording"
  | "clipboard-paste"
  | "open-settings"
  | "set-mouse-sensitivity";

export type NativeOverlayMessage =
  | { kind: "session"; session: NativeOverlaySession }
  | { kind: "stats"; stats: NativeOverlayStats }
  | { kind: "toggles"; toggles: Partial<NativeOverlayToggles> }
  | { kind: "toast"; toast: Omit<NativeOverlayToast, "id"> }
  | { kind: "command"; command: NativeOverlayCommand };

/** Host bridge: implemented by the Tauri adapter and by the preview harness. */
export interface NativeOverlayChannel {
  subscribe(listener: (message: NativeOverlayMessage) => void): () => void;
  dispatch(action: NativeOverlayAction, value?: number): void;
  reportVisibility(state: { deckOpen: boolean; hudVisible: boolean }): void;
  /** Optional: told once the overlay page has mounted, so queued commands flush. */
  signalReady?: () => void;
}

export const emptyOverlaySession: NativeOverlaySession = {
  gameTitle: "GeForce NOW session",
  transport: "nvst",
};

export const emptyOverlayStats: NativeOverlayStats = {};

export const defaultOverlayToggles: NativeOverlayToggles = {
  fullscreen: false,
  micMuted: false,
  recording: false,
  inputCaptured: false,
  hudVisible: false,
  mouseSensitivity: 100,
};

/* ------------------------------- formatting ------------------------------ */

export function formatBitrateMbps(kbps: number | undefined): string {
  if (!kbps || !Number.isFinite(kbps) || kbps <= 0) return "--";
  return (kbps / 1000).toFixed(kbps >= 10_000 ? 0 : 1);
}

export function formatInteger(value: number | undefined): string {
  if (value === undefined || !Number.isFinite(value)) return "--";
  return Math.round(value).toLocaleString("en-US");
}

export function formatElapsed(startedAtMs: number | null | undefined, now: number): string {
  if (!startedAtMs) return "00:00";
  const totalSeconds = Math.max(0, Math.floor((now - startedAtMs) / 1000));
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  const pad = (value: number) => String(value).padStart(2, "0");
  return hours > 0 ? `${hours}:${pad(minutes)}:${pad(seconds)}` : `${pad(minutes)}:${pad(seconds)}`;
}

export function formatCountdown(seconds: number | null | undefined): string | null {
  if (seconds === null || seconds === undefined || !Number.isFinite(seconds)) return null;
  const total = Math.max(0, Math.floor(seconds));
  const hours = Math.floor(total / 3600);
  const minutes = Math.floor((total % 3600) / 60);
  if (hours > 0) return `${hours}h ${String(minutes).padStart(2, "0")}m`;
  return `${minutes}m`;
}

export type MetricTone = "good" | "warn" | "neutral";

export function rttTone(rttMs: number | undefined): MetricTone {
  if (!rttMs || !Number.isFinite(rttMs) || rttMs <= 0) return "neutral";
  if (rttMs < 40) return "good";
  return "warn";
}

export function lossTone(percent: number | undefined): MetricTone {
  if (percent === undefined || !Number.isFinite(percent)) return "neutral";
  if (percent <= 0.5) return "good";
  return "warn";
}

export function fpsTone(fps: number | undefined, target = 60): MetricTone {
  if (!fps || !Number.isFinite(fps)) return "neutral";
  return fps >= target - 2 ? "good" : "warn";
}

/** Shortens a Windows recording path for display. */
export function shortPath(path: string | null | undefined, tail = 34): string {
  if (!path) return "";
  const normalized = path.replace(/\\/g, "/");
  if (normalized.length <= tail) return normalized;
  return `…${normalized.slice(-tail)}`;
}
