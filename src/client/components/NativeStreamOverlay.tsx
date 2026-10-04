import { useEffect, useMemo, useRef, useState } from "react";
import type { JSX, MouseEvent } from "react";
import {
  Activity,
  AudioLines,
  Check,
  ChevronRight,
  CircleAlert,
  Clock3,
  Expand,
  Gamepad2,
  Maximize2,
  Mic,
  MicOff,
  MonitorPlay,
  MousePointer2,
  Pause,
  Radio,
  ShieldCheck,
  Square,
  X,
} from "lucide-react";
import type { StreamDiagnostics } from "../platforms/gfn/webrtcClient";
import { defaultDiagnostics } from "../lib/streamDiagnostics";
import {
  sendNativeStreamOverlayCommand,
  subscribeNativeStreamOverlay,
  postNativeStreamOverlayMessage,
} from "../lib/nativeStreamOverlay";
import type { NativeStreamOverlayState } from "../lib/nativeStreamOverlay";
import { formatBitrate } from "../utils/streamDiagnosticsFormat";
import { getStreamHealthSummary } from "../utils/streamHealthSummary";
import { isShortcutMatch, normalizeShortcut } from "../shortcuts";

type OverlayTab = "overview" | "controls" | "shortcuts";

const EMPTY_STATE: NativeStreamOverlayState = {
  menuOpen: false,
  statsVisible: false,
  gameTitle: "Your game",
  sessionStartedAtMs: null,
  sessionTimeRemainingSeconds: null,
  diagnostics: defaultDiagnostics(),
  isFullscreen: false,
  inputCaptured: false,
  antiAfkEnabled: false,
  microphoneAvailable: false,
  microphoneEnabled: false,
  recording: false,
  shortcuts: {
    toggleStats: "Ctrl+N",
    togglePointerLock: "F8",
    toggleFullscreen: "F11",
    stopStream: "Ctrl+Shift+Q",
    toggleAntiAfk: "Ctrl+Shift+K",
    toggleMicrophone: "Ctrl+Shift+M",
    screenshot: "F11",
    recording: "F12",
    toggleSidebar: "Ctrl+G",
  },
};

function formatClock(seconds: number): string {
  const safeSeconds = Math.max(0, Math.floor(seconds));
  const hours = Math.floor(safeSeconds / 3600);
  const minutes = Math.floor((safeSeconds % 3600) / 60);
  const remainder = safeSeconds % 60;
  return hours > 0
    ? `${hours}:${String(minutes).padStart(2, "0")}:${String(remainder).padStart(2, "0")}`
    : `${minutes}:${String(remainder).padStart(2, "0")}`;
}

function statNumber(value: number, decimals = 0): string {
  return Number.isFinite(value) && value > 0 ? value.toFixed(decimals) : "—";
}

function stopPropagation(event: MouseEvent<HTMLElement>): void {
  event.stopPropagation();
}

function Metric({
  label,
  value,
  unit,
  accent,
  sublabel,
}: {
  label: string;
  value: string;
  unit?: string;
  accent?: "good" | "warn" | "bad" | "blue";
  sublabel?: string;
}): JSX.Element {
  return (
    <div className={`nso-metric${accent ? ` nso-metric--${accent}` : ""}`}>
      <span className="nso-metric__label">{label}</span>
      <span className="nso-metric__value">
        {value}
        {unit && value !== "—" && <small>{unit}</small>}
      </span>
      {sublabel && <span className="nso-metric__sub">{sublabel}</span>}
    </div>
  );
}

function ActionButton({
  icon,
  label,
  detail,
  active,
  disabled,
  tone,
  onClick,
}: {
  icon: JSX.Element;
  label: string;
  detail?: string;
  active?: boolean;
  disabled?: boolean;
  tone?: "danger";
  onClick: () => void;
}): JSX.Element {
  return (
    <button
      type="button"
      className={`nso-action${active ? " is-active" : ""}${tone ? ` nso-action--${tone}` : ""}`}
      onClick={onClick}
      disabled={disabled}
    >
      <span className="nso-action__icon">{icon}</span>
      <span className="nso-action__copy">
        <strong>{label}</strong>
        {detail && <small>{detail}</small>}
      </span>
      {active && <span className="nso-action__state"><Check size={14} /></span>}
      {!active && <ChevronRight size={16} className="nso-action__chevron" />}
    </button>
  );
}

export function NativeStreamOverlay(): JSX.Element {
  const [state, setState] = useState<NativeStreamOverlayState>(EMPTY_STATE);
  const [tab, setTab] = useState<OverlayTab>("overview");
  const [confirmEnd, setConfirmEnd] = useState(false);
  const [now, setNow] = useState(Date.now());
  const closeButtonRef = useRef<HTMLButtonElement | null>(null);

  useEffect(() => {
    const unsubscribe = subscribeNativeStreamOverlay((message) => {
      if (message.type === "state") {
        setState(message.state);
      } else if (message.type === "prompt-end-session") {
        setState((current) => ({ ...current, menuOpen: true }));
        setConfirmEnd(true);
      } else if (message.type === "stats") {
        setState((current) => ({ ...current, diagnostics: message.diagnostics }));
      } else if (message.type === "microphone-state") {
        setState((current) => ({
          ...current,
          microphoneEnabled: message.enabled,
          diagnostics: { ...current.diagnostics, micEnabled: message.enabled, micState: message.state as StreamDiagnostics["micState"] ?? current.diagnostics.micState },
        }));
      }
    });
    postNativeStreamOverlayMessage({ type: "ready" });
    return unsubscribe;
  }, []);

  useEffect(() => {
    if (!state.menuOpen && !state.statsVisible) return undefined;
    const timer = window.setInterval(() => setNow(Date.now()), 1000);
    return () => window.clearInterval(timer);
  }, [state.menuOpen, state.statsVisible]);

  useEffect(() => {
    if (state.menuOpen) {
      const timer = window.setTimeout(() => closeButtonRef.current?.focus(), 60);
      return () => window.clearTimeout(timer);
    }
    setConfirmEnd(false);
    setTab("overview");
    return undefined;
  }, [state.menuOpen]);

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent): void => {
      const matches = (shortcut: string): boolean => isShortcutMatch(event, normalizeShortcut(shortcut));
      if (matches(state.shortcuts.toggleSidebar)) {
        event.preventDefault();
        event.stopPropagation();
        sendNativeStreamOverlayCommand("toggle-menu");
        return;
      }
      if (matches(state.shortcuts.toggleStats)) {
        event.preventDefault();
        event.stopPropagation();
        sendNativeStreamOverlayCommand("toggle-stats");
        return;
      }
      if (matches(state.shortcuts.togglePointerLock)) {
        event.preventDefault();
        event.stopPropagation();
        sendNativeStreamOverlayCommand("toggle-pointer-lock");
        return;
      }
      if (matches(state.shortcuts.toggleFullscreen)) {
        event.preventDefault();
        event.stopPropagation();
        sendNativeStreamOverlayCommand("toggle-fullscreen");
        return;
      }
      if (matches(state.shortcuts.stopStream)) {
        event.preventDefault();
        event.stopPropagation();
        sendNativeStreamOverlayCommand("prompt-stop-stream");
        return;
      }
      if (matches(state.shortcuts.toggleAntiAfk)) {
        event.preventDefault();
        event.stopPropagation();
        sendNativeStreamOverlayCommand("toggle-anti-afk");
        return;
      }
      if (matches(state.shortcuts.toggleMicrophone)) {
        event.preventDefault();
        event.stopPropagation();
        sendNativeStreamOverlayCommand("toggle-microphone");
        return;
      }
      if (matches(state.shortcuts.recording)) {
        event.preventDefault();
        event.stopPropagation();
        sendNativeStreamOverlayCommand("toggle-recording");
        return;
      }
      if (event.key === "Escape") {
        if (confirmEnd) {
          event.preventDefault();
          setConfirmEnd(false);
        } else if (state.menuOpen) {
          event.preventDefault();
          sendNativeStreamOverlayCommand("close-menu");
        } else if (state.statsVisible) {
          event.preventDefault();
          sendNativeStreamOverlayCommand("toggle-stats");
        }
      }
    };
    window.addEventListener("keydown", onKeyDown, true);
    return () => window.removeEventListener("keydown", onKeyDown, true);
  }, [confirmEnd, state.menuOpen, state.statsVisible]);

  const health = useMemo(() => getStreamHealthSummary(state.diagnostics), [state.diagnostics]);
  const elapsed = state.sessionStartedAtMs === null
    ? "00:00"
    : formatClock(Math.max(0, (now - state.sessionStartedAtMs) / 1000));
  const remaining = state.sessionTimeRemainingSeconds === null
    ? "No limit"
    : formatClock(Math.max(0, state.sessionTimeRemainingSeconds));
  const bitrate = state.diagnostics.bitrateKbps > 0
    ? formatBitrate(state.diagnostics.bitrateKbps)
    : state.diagnostics.targetBitrateKbps > 0
      ? `${formatBitrate(state.diagnostics.targetBitrateKbps)} target`
      : "—";
  const lossAccent = state.diagnostics.packetLossPercent < 0.2
    ? "good"
    : state.diagnostics.packetLossPercent < 1
      ? "warn"
      : "bad";
  const sessionHealthClass = `nso-health nso-health--${health.tier}`;
  const heroStyle = state.gameHero || state.gameCover
    ? { backgroundImage: `linear-gradient(90deg, rgba(10,12,17,.96) 4%, rgba(10,12,17,.72) 55%, rgba(10,12,17,.18) 100%), url("${state.gameHero ?? state.gameCover}")` }
    : undefined;

  const closeMenu = (): void => sendNativeStreamOverlayCommand("close-menu");
  const stopSession = (): void => sendNativeStreamOverlayCommand("end-stream");
  const renderStatsCard = (): JSX.Element => (
    <section className="nso-live-stats" aria-label="Live session metrics">
      <div className="nso-live-stats__heading">
        <span className="nso-live-dot" />
        <span>PERFORMANCE</span>
        <b className={sessionHealthClass}>{health.label}</b>
      </div>
      <div className="nso-live-stats__grid">
        <Metric label="FPS" value={statNumber(Math.max(state.diagnostics.decodeFps, state.diagnostics.renderFps))} accent="good" />
        <Metric label="LATENCY" value={statNumber(state.diagnostics.rttMs)} unit="ms" accent={state.diagnostics.rttMs > 0 && state.diagnostics.rttMs < 45 ? "good" : "warn"} />
        <Metric label="BITRATE" value={bitrate.replace(/\s?(Mbps|kbps)( target)?$/, "")} unit={bitrate.includes("Mbps") ? "Mbps" : bitrate.includes("kbps") ? "kbps" : undefined} accent="blue" />
        <Metric label="PACKET LOSS" value={statNumber(state.diagnostics.packetLossPercent, 2)} unit="%" accent={lossAccent} />
      </div>
      <div className="nso-live-stats__footer">
        <span>{state.diagnostics.resolution || "Waiting for video"}</span>
        <span>{state.serverRegion || state.diagnostics.serverRegion || "Native stream"}</span>
      </div>
    </section>
  );

  if (!state.menuOpen && !state.statsVisible) return <div className="nso-root" aria-hidden="true" />;

  return (
    <div className={`nso-root${state.menuOpen ? " nso-root--menu" : " nso-root--stats"}`}>
      {state.menuOpen && (
        <>
          <button className="nso-dismiss" type="button" aria-label="Close session menu" onClick={closeMenu} />
          <aside className="nso-panel" role="dialog" aria-modal="false" aria-label="Stream controls" onClick={stopPropagation}>
            <div className="nso-panel__glow" />
            <header className="nso-topbar">
              <div className="nso-brand">
                <span className="nso-brand__mark"><i /><i /><i /></span>
                <span>Open<span>NOW</span></span>
                <em>SESSION</em>
              </div>
              <div className="nso-topbar__right">
                <span className="nso-native-badge"><span className="nso-live-dot" /> NATIVE</span>
                <button ref={closeButtonRef} className="nso-close" type="button" aria-label="Close menu" onClick={closeMenu}>
                  <X size={18} />
                </button>
              </div>
            </header>

            <section className="nso-hero" style={heroStyle}>
              <div className="nso-hero__content">
                <div className="nso-eyebrow"><span className="nso-live-dot" /> SESSION IN PROGRESS</div>
                <h1>{state.gameTitle || "Your game"}</h1>
                <div className="nso-hero__meta">
                  <span><Clock3 size={13} /> {elapsed} played</span>
                  {state.platformName && <span>{state.platformName}</span>}
                  <span className={sessionHealthClass}><i /> {health.label}</span>
                </div>
              </div>
              <div className="nso-hero__session-time">
                <small>SESSION TIME</small>
                <strong>{remaining}</strong>
              </div>
            </section>

            <nav className="nso-tabs" aria-label="Session menu sections">
              {(["overview", "controls", "shortcuts"] as const).map((item) => (
                <button
                  key={item}
                  className={tab === item ? "is-active" : ""}
                  type="button"
                  onClick={() => setTab(item)}
                >
                  {item === "overview" ? "Overview" : item === "controls" ? "Controls" : "Shortcuts"}
                  {tab === item && <span />}
                </button>
              ))}
              <button
                className={`nso-tabs__stats${state.statsVisible ? " is-active" : ""}`}
                type="button"
                onClick={() => sendNativeStreamOverlayCommand("toggle-stats")}
                aria-pressed={state.statsVisible}
              >
                <Activity size={15} /> Stats
              </button>
            </nav>

            <div className="nso-content">
              {tab === "overview" && (
                <>
                  <div className="nso-section-heading">
                    <div><span>LIVE TELEMETRY</span><small>Updated continuously from the native stream</small></div>
                    <span className={sessionHealthClass}><i /> {health.label}</span>
                  </div>
                  <div className="nso-metrics-grid">
                    <Metric label="FRAME RATE" value={statNumber(Math.max(state.diagnostics.decodeFps, state.diagnostics.renderFps))} unit="FPS" accent="good" sublabel="Presented frames" />
                    <Metric label="ROUND TRIP" value={statNumber(state.diagnostics.rttMs)} unit="ms" accent={state.diagnostics.rttMs > 0 && state.diagnostics.rttMs < 45 ? "good" : "warn"} sublabel="Game server latency" />
                    <Metric label="STREAM RATE" value={bitrate.replace(/\s?(Mbps|kbps)( target)?$/, "")} unit={bitrate.includes("Mbps") ? "Mbps" : bitrate.includes("kbps") ? "kbps" : undefined} accent="blue" sublabel="Video bitrate" />
                    <Metric label="PACKET LOSS" value={statNumber(state.diagnostics.packetLossPercent, 2)} unit="%" accent={lossAccent} sublabel={state.diagnostics.nativeRendererActive ? "Estimated frame loss" : "Network packets"} />
                  </div>
                  <div className="nso-detail-row">
                    <div className="nso-detail-row__item"><span>RESOLUTION</span><strong>{state.diagnostics.resolution || "Negotiating"}</strong></div>
                    <div className="nso-detail-row__item"><span>VIDEO CODEC</span><strong>{state.diagnostics.codec || "NVST"}</strong></div>
                    <div className="nso-detail-row__item"><span>SERVER</span><strong>{state.serverRegion || state.diagnostics.serverRegion || "Auto"}</strong></div>
                  </div>
                  {state.diagnostics.lagReasonDetail && state.diagnostics.lagReason !== "unknown" && state.diagnostics.lagReason !== "stable" && (
                    <div className="nso-health-note"><CircleAlert size={15} /><span>{state.diagnostics.lagReasonDetail}</span></div>
                  )}
                  <div className="nso-controls-heading"><span>QUICK CONTROLS</span><span>YOUR SESSION, YOUR WAY</span></div>
                  <div className="nso-actions-grid">
                    <ActionButton icon={<Maximize2 size={18} />} label={state.isFullscreen ? "Exit fullscreen" : "Go fullscreen"} detail={state.shortcuts.toggleFullscreen} active={state.isFullscreen} onClick={() => sendNativeStreamOverlayCommand("toggle-fullscreen")} />
                    <ActionButton icon={state.recording ? <Square size={17} /> : <Radio size={18} />} label={state.recording ? "Stop recording" : "Start recording"} detail={state.recording ? "Recording locally" : state.shortcuts.recording} active={state.recording} onClick={() => sendNativeStreamOverlayCommand("toggle-recording")} />
                    <ActionButton icon={state.microphoneEnabled ? <Mic size={18} /> : <MicOff size={18} />} label={state.microphoneEnabled ? "Microphone on" : "Microphone off"} detail={state.microphoneAvailable ? state.shortcuts.toggleMicrophone : "Enable mic in Settings"} active={state.microphoneEnabled} disabled={!state.microphoneAvailable} onClick={() => sendNativeStreamOverlayCommand("toggle-microphone")} />
                    <ActionButton icon={state.antiAfkEnabled ? <ShieldCheck size={18} /> : <Pause size={18} />} label="Anti-AFK" detail={state.antiAfkEnabled ? "Keeps your session active" : state.shortcuts.toggleAntiAfk} active={state.antiAfkEnabled} onClick={() => sendNativeStreamOverlayCommand("toggle-anti-afk")} />
                  </div>
                </>
              )}

              {tab === "controls" && (
                <>
                  <div className="nso-section-heading">
                    <div><span>INPUT & DISPLAY</span><small>Hardware-accelerated native presentation stays active</small></div>
                    <MonitorPlay size={18} />
                  </div>
                  <div className="nso-control-status">
                    <div className="nso-control-status__icon"><MousePointer2 size={18} /></div>
                    <div><strong>Mouse capture</strong><small>{state.inputCaptured ? "Captured for gameplay" : "Free cursor · menu-safe"}</small></div>
                    <span className={`nso-status-pill${state.inputCaptured ? " is-live" : ""}`}>{state.inputCaptured ? "LOCKED" : "FREE"}</span>
                  </div>
                  <div className="nso-control-status">
                    <div className="nso-control-status__icon"><AudioLines size={18} /></div>
                    <div><strong>Audio output</strong><small>Native low-latency playback</small></div>
                    <span className="nso-status-pill is-live">ACTIVE</span>
                  </div>
                  <div className="nso-control-status">
                    <div className="nso-control-status__icon"><Gamepad2 size={18} /></div>
                    <div><strong>Controller input</strong><small>Connected devices are forwarded directly</small></div>
                    <span className="nso-status-pill is-live">READY</span>
                  </div>
                  <div className="nso-info-card"><ShieldCheck size={17} /><span>Opening this menu releases game input; video and audio continue on the native stream path.</span></div>
                  <button className="nso-wide-action" type="button" onClick={() => sendNativeStreamOverlayCommand("toggle-pointer-lock")}>
                    <MousePointer2 size={17} /> Toggle mouse capture on return to gameplay <span>{state.shortcuts.togglePointerLock}</span>
                  </button>
                  <button className="nso-wide-action" type="button" onClick={() => sendNativeStreamOverlayCommand("toggle-fullscreen")}>
                    <Expand size={17} /> {state.isFullscreen ? "Return to windowed mode" : "Switch to fullscreen"} <span>{state.shortcuts.toggleFullscreen}</span>
                  </button>
                </>
              )}

              {tab === "shortcuts" && (
                <>
                  <div className="nso-section-heading"><div><span>KEYBOARD SHORTCUTS</span><small>Press a shortcut any time during gameplay</small></div><Gamepad2 size={18} /></div>
                  <div className="nso-shortcut-list">
                    <div><span>Open or close menu</span><kbd>{state.shortcuts.toggleSidebar}</kbd></div>
                    <div><span>Toggle performance stats</span><kbd>{state.shortcuts.toggleStats}</kbd></div>
                    <div><span>Toggle mouse capture</span><kbd>{state.shortcuts.togglePointerLock}</kbd></div>
                    <div><span>Toggle fullscreen</span><kbd>{state.shortcuts.toggleFullscreen}</kbd></div>
                    <div><span>Toggle Anti-AFK</span><kbd>{state.shortcuts.toggleAntiAfk}</kbd></div>
                    <div><span>Toggle microphone</span><kbd>{state.shortcuts.toggleMicrophone}</kbd></div>
                    <div><span>Start / stop recording</span><kbd>{state.shortcuts.recording}</kbd></div>
                    <div><span>End session</span><kbd>{state.shortcuts.stopStream}</kbd></div>
                  </div>
                </>
              )}
            </div>

            <footer className="nso-footer">
              <div className="nso-footer__status"><span className="nso-live-dot" /> STREAM CONNECTED <span className="nso-footer__separator">·</span> {state.diagnostics.hardwareAcceleration || "Native video path"}</div>
              <button type="button" className="nso-end-button" onClick={() => setConfirmEnd(true)}>End session</button>
            </footer>

            {confirmEnd && (
              <div className="nso-confirm-backdrop" role="presentation" onClick={() => setConfirmEnd(false)}>
                <section className="nso-confirm" role="alertdialog" aria-modal="true" aria-labelledby="nso-confirm-title" onClick={stopPropagation}>
                  <div className="nso-confirm__icon"><CircleAlert size={21} /></div>
                  <h2 id="nso-confirm-title">End this session?</h2>
                  <p>Your game session will close. This action cannot be undone.</p>
                  <div className="nso-confirm__actions">
                    <button type="button" onClick={() => setConfirmEnd(false)}>Keep playing</button>
                    <button type="button" className="is-danger" onClick={stopSession}>End session</button>
                  </div>
                </section>
              </div>
            )}
          </aside>
        </>
      )}

      {state.statsVisible && (
        <div className={`nso-stats-anchor${state.menuOpen ? " nso-stats-anchor--menu" : ""}`}>
          {renderStatsCard()}
        </div>
      )}
    </div>
  );
}
