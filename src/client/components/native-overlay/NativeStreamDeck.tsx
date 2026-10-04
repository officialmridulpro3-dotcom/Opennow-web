import { useMemo, type CSSProperties, type JSX, type ReactNode } from "react";
import {
  Activity,
  ChevronRight,
  Clipboard,
  Clock,
  Cpu,
  Gauge,
  Gamepad2,
  Keyboard,
  ListVideo,
  MapPin,
  Maximize2,
  Mic,
  MicOff,
  Minimize2,
  MonitorPlay,
  MousePointer2,
  Power,
  Radio,
  Settings2,
  Sparkles,
  Volume2,
  Wifi,
  X,
  Zap,
} from "lucide-react";
import {
  formatBitrateMbps,
  formatCountdown,
  formatElapsed,
  formatInteger,
  fpsTone,
  lossTone,
  rttTone,
  shortPath,
  type MetricTone,
  type NativeOverlayAction,
  type NativeOverlaySession,
  type NativeOverlayStats,
  type NativeOverlayTab,
  type NativeOverlayToggles,
} from "./types";

const TABS: { id: NativeOverlayTab; label: string }[] = [
  { id: "session", label: "Session" },
  { id: "controls", label: "Controls" },
  { id: "media", label: "Media" },
  { id: "keys", label: "Keys" },
];

export interface NativeStreamDeckProps {
  visible: boolean;
  session: NativeOverlaySession;
  stats: NativeOverlayStats;
  toggles: NativeOverlayToggles;
  tab: NativeOverlayTab;
  now: number;
  onTabChange: (tab: NativeOverlayTab) => void;
  onAction: (action: NativeOverlayAction, value?: number) => void;
  onClose: () => void;
  onToggleHud: () => void;
  onRequestEnd: () => void;
}

/**
 * The stream deck: the full sidebar that opens over the native video window
 * (Ctrl+G, gamepad Guide, or the "Show deck" hotkey in the main window).
 *
 * It replaces the engine's GDI-painted menu with real DOM: gradients, blur,
 * chamfered cells, animated sheen, and the same accent token as the rest of
 * the client — none of which a Win32 `DrawTextW` panel can express.
 */
export function NativeStreamDeck({
  visible,
  session,
  stats,
  toggles,
  tab,
  now,
  onTabChange,
  onAction,
  onClose,
  onToggleHud,
  onRequestEnd,
}: NativeStreamDeckProps): JSX.Element | null {
  const elapsed = formatElapsed(session.startedAtMs, now);
  const countdown = formatCountdown(session.remainingSeconds);
  const hero = session.heroImageUrl ?? session.coverImageUrl ?? null;
  const title = session.gameTitle?.trim() || "GeForce NOW session";

  const resolution = stats.resolution?.trim() || null;
  const streamFps = stats.renderFps ?? stats.decodedFps;

  const shortcutRows = useMemo(
    () => [
      { label: "Stream deck (this menu)", keys: ["Ctrl", "G"] },
      { label: "Live stats HUD", keys: ["Ctrl", "N"] },
      { label: "Start / stop recording", keys: ["F12"] },
      { label: "Toggle fullscreen", keys: ["Alt", "Enter"] },
      { label: "Paste clipboard to game", keys: ["Ctrl", "V"] },
      { label: "Release / capture mouse", keys: ["Ctrl", "Shift", "M"] },
      { label: "End session", keys: ["Ctrl", "Shift", "Q"] },
    ],
    [],
  );

  if (!visible) return null;

  return (
    <aside className="nov-deck" aria-label="OpenNOW stream deck">
      <header className="nov-deck__top">
        <div className="nov-brand">
          <img className="nov-brand__mark" src="/opennow-logo-mark.png" alt="" aria-hidden="true" />
          <span className="nov-brand__text">
            <span className="nov-brand__title">Stream deck</span>
            <span className="nov-brand__sub">
              {session.transport === "webrtc" ? "WebRTC · browser" : "Native · NVST"}
            </span>
          </span>
        </div>
        <span
          className={`nov-live${session.firstFrame ? "" : " nov-live--idle"}`}
          title={session.firstFrame ? "Video is flowing" : "Waiting for the first decoded frame"}
        >
          <span className="nov-live__dot" />
          {session.firstFrame ? "Live" : "Linking"}
        </span>
        <button type="button" className="nov-icon-btn" onClick={onClose} aria-label="Close stream deck">
          <X size={15} />
        </button>
      </header>

      <div className="nov-hero">
        {hero ? (
          <div className="nov-hero__art" style={{ backgroundImage: `url(${hero})` }} aria-hidden="true" />
        ) : null}
        <div className="nov-hero__veil" aria-hidden="true" />
        <div className="nov-hero__sheen" aria-hidden="true" />
        <div className="nov-hero__body">
          <h2 className="nov-hero__title">{title}</h2>
          <div className="nov-hero__meta">
            <Chip mono icon={<Clock size={11} />}>
              {elapsed}
            </Chip>
            {session.store ? <Chip>{storeLabel(session.store)}</Chip> : null}
            {session.region ? (
              <Chip icon={<MapPin size={11} />}>{session.region}</Chip>
            ) : null}
            {stats.codec ? <Chip accent>{stats.codec.toUpperCase()}</Chip> : null}
            {countdown ? <Chip azure>Ends in {countdown}</Chip> : null}
          </div>
        </div>
      </div>

      <nav className="nov-deck__tabs" role="tablist" aria-label="Stream deck sections">
        {TABS.map((entry) => (
          <button
            key={entry.id}
            type="button"
            role="tab"
            id={`nov-tab-${entry.id}`}
            aria-selected={tab === entry.id}
            aria-controls={`nov-panel-${entry.id}`}
            className="nov-deck__tab"
            onClick={() => onTabChange(entry.id)}
          >
            {entry.label}
          </button>
        ))}
      </nav>

      <div className="nov-deck__body">
        {tab === "session" ? (
          <div role="tabpanel" id="nov-panel-session" aria-labelledby="nov-tab-session" className="nov-section">
            <div className="nov-grid">
              <Metric
                icon={<MonitorPlay size={12} />}
                label="Resolution"
                value={resolution ? resolution.split(" · ")[0].replace("x", "×") : "--"}
                foot={stats.hardwareAcceleration || "decoder warming up"}
              />
              <Metric
                icon={<Gauge size={12} />}
                label="Framerate"
                value={streamFps ? formatInteger(streamFps) : "--"}
                unit={streamFps ? "fps" : undefined}
                tone={fpsTone(streamFps)}
                foot={
                  stats.framesDropped && stats.framesDropped > 0
                    ? `${formatInteger(stats.framesDropped)} frames dropped`
                    : "no dropped frames"
                }
              />
              <Metric
                icon={<Wifi size={12} />}
                label="Bitrate"
                value={formatBitrateMbps(stats.bitrateKbps)}
                unit="Mbps"
                foot={
                  stats.targetBitrateKbps
                    ? `target ${formatBitrateMbps(stats.targetBitrateKbps)} Mbps`
                    : `${stats.queueMode ?? "adaptive"} pacing`
                }
              />
              <Metric
                icon={<Activity size={12} />}
                label="Latency"
                value={stats.rttMs && stats.rttMs > 0 ? formatInteger(stats.rttMs) : "--"}
                unit="ms"
                tone={rttTone(stats.rttMs)}
                foot={`network loss ${(stats.packetLossPercent ?? 0).toFixed(1)}%`}
              />
            </div>

            <SectionLabel>Quick actions</SectionLabel>
            <div className="nov-rows">
              <ActionRow
                icon={toggles.fullscreen ? <Minimize2 size={15} /> : <Maximize2 size={15} />}
                title={toggles.fullscreen ? "Leave fullscreen" : "Go fullscreen"}
                sub="Alt + Enter"
                onClick={() => onAction("toggle-fullscreen")}
              />
              <ActionRow
                icon={<MousePointer2 size={15} />}
                title={toggles.inputCaptured ? "Release mouse to desktop" : "Capture mouse in game"}
                sub={toggles.inputCaptured ? "pointer is locked to the session" : "cursor is free"}
                pressed={toggles.inputCaptured}
                onClick={() => onAction("capture-mouse")}
              />
              <ActionRow
                icon={<Zap size={15} />}
                title={toggles.hudVisible ? "Hide live stats" : "Show live stats"}
                sub="Ctrl + N"
                pressed={toggles.hudVisible}
                onClick={onToggleHud}
              />
            </div>

            <SectionLabel>Session</SectionLabel>
            <div className="nov-rows">
              <InfoRow icon={<Radio size={15} />} title="Transport" value={session.transport === "webrtc" ? "WebRTC" : "NVST (native)"} />
              <InfoRow
                icon={<Cpu size={15} />}
                title="Decoder"
                value={stats.hardwareAcceleration || (stats.zeroCopy ? "D3D11 zero-copy" : "hardware")}
              />
              <InfoRow
                icon={<Sparkles size={15} />}
                title="Engine"
                value={
                  session.enginePid
                    ? `pid ${session.enginePid}${session.protocolVersion ? ` · protocol v${session.protocolVersion}` : ""}`
                    : "starting"
                }
              />
              <InfoRow
                icon={<Activity size={15} />}
                title="Packet loss"
                value={`${(stats.packetLossPercent ?? 0).toFixed(1)}%`}
                tone={lossTone(stats.packetLossPercent)}
              />
            </div>

            <SectionLabel>Danger zone</SectionLabel>
            <div className="nov-actions">
              <button type="button" className="nov-btn nov-btn--ghost-danger nov-btn--grow" onClick={onRequestEnd}>
                <Power size={14} /> End session
              </button>
            </div>
          </div>
        ) : null}

        {tab === "controls" ? (
          <div role="tabpanel" id="nov-panel-controls" aria-labelledby="nov-tab-controls" className="nov-section">
            <SectionLabel>Pointer &amp; input</SectionLabel>
            <div className="nov-rows">
              <SwitchRow
                icon={<MousePointer2 size={15} />}
                title="Capture mouse"
                sub="lock the cursor to the game window"
                on={toggles.inputCaptured}
                onClick={() => onAction("capture-mouse")}
              />
              <SliderRow
                icon={<Gauge size={15} />}
                label="Mouse sensitivity"
                value={toggles.mouseSensitivity}
                min={25}
                max={200}
                step={5}
                suffix="%"
                onChange={(value) => onAction("set-mouse-sensitivity", value)}
              />
            </div>

            <SectionLabel>Gamepad</SectionLabel>
            <div className="nov-rows">
              <InfoRow icon={<Gamepad2 size={15} />} title="Controllers" value="auto-detect at launch" />
              <ActionRow
                icon={<Gamepad2 size={15} />}
                title="Guide button opens this deck"
                sub="same behaviour as the GeForce NOW client"
                onClick={onClose}
              />
            </div>

            <SectionLabel>Session behaviour</SectionLabel>
            <div className="nov-rows">
              <ActionRow
                icon={<Clipboard size={15} />}
                title="Send clipboard to the game"
                sub="Ctrl + V · paste into Steam login, chat, launchers"
                onClick={() => onAction("clipboard-paste")}
              />
              <ActionRow
                icon={<Settings2 size={15} />}
                title="Stream settings"
                sub="quality, codec and bandwidth live in the main window"
                onClick={() => onAction("open-settings")}
              />
            </div>
          </div>
        ) : null}

        {tab === "media" ? (
          <div role="tabpanel" id="nov-panel-media" aria-labelledby="nov-tab-media" className="nov-section">
            <SectionLabel>Microphone</SectionLabel>
            <div className="nov-rows">
              <SwitchRow
                icon={toggles.micMuted ? <MicOff size={15} /> : <Mic size={15} />}
                title={toggles.micMuted ? "Microphone muted" : "Microphone live"}
                sub="push-to-talk follows the stream settings"
                on={!toggles.micMuted}
                onClick={() => onAction("toggle-mic")}
              />
            </div>

            <SectionLabel>Audio</SectionLabel>
            <div className="nov-rows">
              <InfoRow
                icon={<Volume2 size={15} />}
                title="Output device"
                value="engine default · change it in Stream settings"
              />
            </div>

            <SectionLabel>Capture</SectionLabel>
            <div className="nov-rows">
              <ActionRow
                icon={<ListVideo size={15} />}
                title={toggles.recording ? "Stop recording" : "Start recording"}
                sub={
                  toggles.recording && session.recording
                    ? shortPath(session.recording.path, 38)
                    : "F12 · MKV, video + audio in the recordings folder"
                }
                pressed={toggles.recording}
                onClick={() => onAction("toggle-recording")}
              />
              <InfoRow
                icon={<ListVideo size={15} />}
                title="Capture folder"
                value={
                  session.recording
                    ? shortPath(session.recording.path, 40)
                    : "clips land in the OpenNOW recordings folder"
                }
              />
            </div>
          </div>
        ) : null}

        {tab === "keys" ? (
          <div role="tabpanel" id="nov-panel-keys" aria-labelledby="nov-tab-keys" className="nov-section">
            <SectionLabel>Shortcuts</SectionLabel>
            <div className="nov-shortcuts">
              {shortcutRows.map((row) => (
                <div className="nov-shortcut" key={row.label}>
                  <Keyboard size={13} aria-hidden="true" />
                  <span className="nov-shortcut__label">{row.label}</span>
                  <span className="nov-shortcut__keys">
                    {row.keys.map((key) => (
                      <kbd className="nov-keycap" key={key}>
                        {key}
                      </kbd>
                    ))}
                  </span>
                </div>
              ))}
            </div>
            <p className="nov-row__sub" style={{ padding: "2px 2px 0" }}>
              Bindings are editable in Settings → Stream. The deck always answers to the Guide
              button, so a controller works without a keyboard.
            </p>
          </div>
        ) : null}
      </div>

      <footer className="nov-deck__foot">
        <span className="nov-deck__engine">
          <Zap size={11} aria-hidden="true" />{" "}
          {stats.zeroCopy === true ? "zero-copy path" : stats.zeroCopy === false ? "copy path" : "hardware path"}
        </span>
        <span className="nov-dot-sep" aria-hidden="true" />
        <span>
          <strong>{formatInteger(stats.framesDecoded)}</strong> frames
        </span>
        <span className="nov-dot-sep" aria-hidden="true" />
        <button type="button" className="nov-icon-btn" onClick={onClose} aria-label="Resume stream">
          <ChevronRight size={15} />
        </button>
      </footer>
    </aside>
  );
}

/* ------------------------------ subcomponents ----------------------------- */

/** Store id → the label players actually recognise. */
function storeLabel(store: string): string {
  const normalized = store.toUpperCase();
  if (normalized.includes("STEAM")) return "Steam";
  if (normalized.includes("EPIC")) return "Epic";
  if (normalized.includes("XBOX") || normalized.includes("MICROSOFT")) return "Xbox";
  if (normalized.includes("GOG")) return "GOG";
  if (normalized.includes("UBISOFT")) return "Ubisoft";
  if (normalized.includes("EA") || normalized.includes("ORIGIN")) return "EA";
  return store;
}

function Chip({
  children,
  icon,
  accent,
  azure,
  mono,
}: {
  children: ReactNode;
  icon?: ReactNode;
  accent?: boolean;
  azure?: boolean;
  mono?: boolean;
}): JSX.Element {
  const classes = [
    "nov-chip",
    accent ? "nov-chip--accent" : "",
    azure ? "nov-chip--azure" : "",
    mono ? "nov-chip--mono" : "",
  ]
    .filter(Boolean)
    .join(" ");
  return (
    <span className={classes}>
      {icon}
      {children}
    </span>
  );
}

function SectionLabel({ children }: { children: ReactNode }): JSX.Element {
  return <div className="nov-section__label">{children}</div>;
}

function Metric({
  icon,
  label,
  value,
  unit,
  foot,
  tone = "neutral",
}: {
  icon: ReactNode;
  label: string;
  value: string;
  unit?: string;
  foot?: string;
  tone?: MetricTone;
}): JSX.Element {
  return (
    <div className="nov-metric" data-tone={tone}>
      <div className="nov-metric__label">
        {icon}
        {label}
      </div>
      <div className="nov-metric__value">
        {value}
        {unit ? <span className="nov-metric__unit">{unit}</span> : null}
      </div>
      {foot ? <div className="nov-metric__foot">{foot}</div> : null}
    </div>
  );
}

function ActionRow({
  icon,
  title,
  sub,
  onClick,
  pressed,
  danger,
}: {
  icon: ReactNode;
  title: string;
  sub?: string;
  onClick: () => void;
  pressed?: boolean;
  danger?: boolean;
}): JSX.Element {
  return (
    <button
      type="button"
      className="nov-row"
      onClick={onClick}
      aria-pressed={pressed}
      data-danger={danger ? "true" : undefined}
    >
      <span className="nov-row__icon">{icon}</span>
      <span className="nov-row__text">
        <span className="nov-row__title">{title}</span>
        {sub ? <span className="nov-row__sub">{sub}</span> : null}
      </span>
      <span className="nov-row__trail">
        <ChevronRight size={14} aria-hidden="true" />
      </span>
    </button>
  );
}

function InfoRow({
  icon,
  title,
  value,
  tone = "neutral",
}: {
  icon: ReactNode;
  title: string;
  value: string;
  tone?: MetricTone;
}): JSX.Element {
  return (
    <div className="nov-row" style={{ cursor: "default" }}>
      <span className="nov-row__icon">{icon}</span>
      <span className="nov-row__text">
        <span className="nov-row__title">{title}</span>
        <span className="nov-row__sub">{value}</span>
      </span>
      {tone !== "neutral" ? (
        <span className="nov-row__trail">
          <span className={tone === "good" ? "nov-chip nov-chip--accent" : "nov-chip nov-chip--azure"}>
            {tone === "good" ? "stable" : "check"}
          </span>
        </span>
      ) : null}
    </div>
  );
}

function SwitchRow({
  icon,
  title,
  sub,
  on,
  onClick,
}: {
  icon: ReactNode;
  title: string;
  sub?: string;
  on: boolean;
  onClick: () => void;
}): JSX.Element {
  return (
    <button type="button" className="nov-row" role="switch" aria-checked={on} onClick={onClick}>
      <span className="nov-row__icon">{icon}</span>
      <span className="nov-row__text">
        <span className="nov-row__title">{title}</span>
        {sub ? <span className="nov-row__sub">{sub}</span> : null}
      </span>
      <span className="nov-row__trail">
        <span className="nov-switch" data-on={on ? "true" : "false"} aria-hidden="true" />
      </span>
    </button>
  );
}

function SliderRow({
  icon,
  label,
  value,
  suffix,
  min = 0,
  max = 100,
  step = 1,
  onChange,
}: {
  icon: ReactNode;
  label: string;
  value: number;
  suffix?: string;
  min?: number;
  max?: number;
  step?: number;
  onChange: (value: number) => void;
}): JSX.Element {
  const fill = Math.round(((value - min) / Math.max(1, max - min)) * 100);
  return (
    <div className="nov-slider">
      <span className="nov-slider__label">
        {icon}
        {label}
      </span>
      <input
        type="range"
        min={min}
        max={max}
        step={step}
        value={value}
        style={{ "--nov-slider-fill": `${fill}%` } as CSSProperties}
        onChange={(event) => onChange(Number(event.target.value))}
        aria-label={label}
      />
      <span className="nov-slider__value">
        {value}
        {suffix ?? ""}
      </span>
    </div>
  );
}
