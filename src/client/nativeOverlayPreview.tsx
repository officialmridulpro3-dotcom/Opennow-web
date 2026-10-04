import { StrictMode, useEffect, useMemo, useRef, useState, type ReactElement } from "react";
import { createRoot } from "react-dom/client";
import { MotionProvider } from "./components/MotionProvider";
import { NativeOverlayRoot } from "./components/native-overlay/NativeOverlayRoot";
import type {
  NativeOverlayAction,
  NativeOverlayChannel,
  NativeOverlayMessage,
} from "./components/native-overlay/types";
import "./styles.css";
import "./nativeOverlayPreview.css";

/**
 * Preview harness for the native stream overlay.
 *
 * Runs the *real* overlay components (deck, stats HUD, toasts, exit dialog)
 * against a mock host channel on top of a synthetic "gameplay" backdrop, so the
 * overlay can be reviewed in a browser instead of only inside a Windows build
 * with a live GeForce NOW seat.
 *
 *   npm run preview:overlay  →  http://localhost:5173/native-overlay-preview.html
 */

interface MockChannel extends NativeOverlayChannel {
  /** Push a host → overlay message (as the Tauri shell would). */
  send: (message: NativeOverlayMessage) => void;
  /** Subscribe to overlay → host actions so the harness can log them. */
  onAction: (listener: (action: NativeOverlayAction, value?: number) => void) => () => void;
}

function createMockChannel(): MockChannel {
  const listeners = new Set<(message: NativeOverlayMessage) => void>();
  const actionListeners = new Set<(action: NativeOverlayAction, value?: number) => void>();
  return {
    send(message) {
      listeners.forEach((listener) => listener(message));
    },
    subscribe(listener) {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
    dispatch(action, value) {
      actionListeners.forEach((listener) => listener(action, value));
    },
    reportVisibility() {
      /* the harness keeps everything visible */
    },
    onAction(listener) {
      actionListeners.add(listener);
      return () => actionListeners.delete(listener);
    },
  };
}

/** A believable NVST session: ~44 Mbps 1440p60 with small jitter. */
function useMockSessionFeed(channel: MockChannel): void {
  const startedAt = useMemo(() => Date.now() - 42 * 60 * 1000, []);
  const frames = useRef(184_320);

  useEffect(() => {
    channel.send({
      kind: "session",
      session: {
        gameTitle: "Cyberpunk 2077",
        heroImageUrl:
          "https://cdn.cloudflare.steamstatic.com/steam/apps/1091500/library_hero.jpg",
        store: "STEAM",
        region: "EU West · Paris",
        startedAtMs: startedAt,
        transport: "nvst",
        enginePid: 18324,
        protocolVersion: 7,
        firstFrame: true,
        remainingSeconds: 5 * 3600 + 42 * 60,
      },
    });
    channel.send({
      kind: "toggles",
      toggles: { fullscreen: false, micMuted: false, inputCaptured: true, mouseSensitivity: 100 },
    });

    let tick = 0;
    const timer = window.setInterval(() => {
      tick += 1;
      frames.current += 58 + Math.round(Math.random() * 3);
      const jitter = Math.sin(tick / 6) * 3;
      channel.send({
        kind: "stats",
        stats: {
          codec: "h264",
          resolution: "2560x1440",
          decodedFps: 59.4 + jitter / 6,
          renderFps: 59.1 + jitter / 6,
          bitrateKbps: 43_800 + jitter * 620,
          targetBitrateKbps: 45_000,
          rttMs: 18 + Math.abs(jitter),
          packetLossPercent: Math.max(0, 0.2 - jitter / 40),
          framesDecoded: frames.current,
          framesDropped: tick > 12 ? 3 : 0,
          hardwareAcceleration: "D3D11VA · zero-copy",
          queueMode: "adaptive",
          zeroCopy: true,
          updatedAtMs: Date.now(),
        },
      });
    }, 1000);

    return () => window.clearInterval(timer);
  }, [channel, startedAt]);
}

const PREVIEW_ACTIONS: { label: string; action: NativeOverlayAction }[] = [
  { label: "Record", action: "toggle-recording" },
  { label: "Mic", action: "toggle-mic" },
  { label: "Fullscreen", action: "toggle-fullscreen" },
  { label: "Capture mouse", action: "capture-mouse" },
  { label: "Clipboard", action: "clipboard-paste" },
];

function PreviewApp(): ReactElement {
  const channel = useMemo(createMockChannel, []);
  const [log, setLog] = useState<string[]>([]);
  const [recording, setRecording] = useState(false);

  useMockSessionFeed(channel);

  useEffect(
    () =>
      channel.onAction((action, value) => {
        setLog((current) =>
          [`${new Date().toLocaleTimeString()}  ${action}${value !== undefined ? ` = ${value}` : ""}`, ...current].slice(0, 7),
        );
        if (action === "toggle-recording") {
          setRecording((on) => {
            channel.send({
              kind: "toast",
              toast: on
                ? {
                    kind: "success",
                    title: "Recording saved",
                    detail:
                      "%USERPROFILE%\\Videos\\OpenNOW\\Cyberpunk 2077 2026-10-04 21-14-08.mkv",
                  }
                : { kind: "warning", title: "Recording started", detail: "MKV · video + audio" },
            });
            channel.send({ kind: "toggles", toggles: { recording: !on } });
            return !on;
          });
        }
        if (action === "clipboard-paste") {
          channel.send({
            kind: "toast",
            toast: {
              kind: "info",
              title: "Clipboard sent to the game",
              detail: "5 lines · pasted into the focused remote window",
            },
          });
        }
        if (action === "toggle-mic") {
          channel.send({ kind: "toast", toast: { kind: "info", title: "Microphone toggled" } });
        }
        if (action === "end-session") {
          channel.send({
            kind: "toast",
            toast: { kind: "warning", title: "Session ended", detail: "Seat released on the GeForce NOW rig" },
          });
        }
        if (action === "resume") {
          channel.send({ kind: "toast", toast: { kind: "info", title: "Stream resumed" } });
        }
      }),
    [channel],
  );

  return (
    <>
      <SyntheticGameplay />
      <NativeOverlayRoot channel={channel} initialHudVisible />

      <div className="novp-panel">
        <div className="novp-panel__title">
          Overlay preview <span>web layer · DOM + CSS</span>
        </div>
        <div className="novp-panel__row">
          <button type="button" onClick={() => channel.send({ kind: "command", command: "toggle-deck" })}>
            Ctrl + G · deck
          </button>
          <button type="button" onClick={() => channel.send({ kind: "command", command: "toggle-hud" })}>
            Ctrl + N · stats
          </button>
        </div>
        <div className="novp-panel__row">
          {PREVIEW_ACTIONS.map((entry) => (
            <button
              key={entry.action}
              type="button"
              className={entry.action === "toggle-recording" && recording ? "is-active" : ""}
              onClick={() => channel.dispatch(entry.action)}
            >
              {entry.label}
            </button>
          ))}
        </div>
        {log.length > 0 ? (
          <pre className="novp-panel__log">{log.join("\n")}</pre>
        ) : (
          <div className="novp-panel__hint">
            Overlay → host actions appear here. Everything on screen is real overlay UI.
          </div>
        )}
      </div>
    </>
  );
}

/** Offline-safe stand-in for the native video plane. */
function SyntheticGameplay(): ReactElement {
  return (
    <div className="novp-game" aria-hidden="true">
      <div className="novp-game__sky" />
      <div className="novp-game__sun" />
      <div className="novp-game__city" />
      <div className="novp-game__grid" />
      <div className="novp-game__vignette" />
      <div className="novp-game__hud">
        <div className="novp-game__health">
          <span />
        </div>
        <div className="novp-game__objective">OBJECTIVE · deliver the shard to Wakako</div>
        <div className="novp-game__minimap" />
        <div className="novp-game__crosshair" />
      </div>
      <div className="novp-game__label">native video plane — rendered by the Rust/D3D11 engine</div>
    </div>
  );
}

createRoot(document.getElementById("root") as HTMLElement).render(
  <StrictMode>
    <MotionProvider>
      <PreviewApp />
    </MotionProvider>
  </StrictMode>,
);
