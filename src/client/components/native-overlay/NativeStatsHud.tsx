import { useMemo, type JSX } from "react";
import { Activity, ChevronDown, ChevronUp, Gauge, Wifi, X } from "lucide-react";
import {
  formatBitrateMbps,
  formatInteger,
  fpsTone,
  lossTone,
  rttTone,
  type NativeOverlaySession,
  type NativeOverlayStats,
} from "./types";

export interface NativeStatsHudProps {
  visible: boolean;
  stats: NativeOverlayStats;
  session: NativeOverlaySession;
  /** Rolling history for the sparkline (oldest → newest). */
  history: number[];
  expanded: boolean;
  onToggleExpanded: () => void;
  onClose: () => void;
}

/**
 * Compact live stats, modelled on the GeForce NOW overlay: three big numbers
 * (game fps / stream fps / ping) over a rolling bitrate graph, with an
 * expandable detail block. Rendered in the same transparent overlay window as
 * the deck so it can sit over the native video plane.
 */
export function NativeStatsHud({
  visible,
  stats,
  session,
  history,
  expanded,
  onToggleExpanded,
  onClose,
}: NativeStatsHudProps): JSX.Element | null {
  const streamFps = stats.renderFps ?? stats.decodedFps;
  const sparkline = useMemo(() => buildSparkline(history), [history]);

  if (!visible) return null;

  const resolution = stats.resolution?.trim();
  const loss = stats.packetLossPercent ?? 0;

  return (
    <section className="nov-hud" aria-label="Live stream statistics">
      <header className="nov-hud__head">
        <Gauge size={13} aria-hidden="true" />
        <span className="nov-hud__title">Performance</span>
        <span className="nov-hud__spacer" />
        {resolution ? <span className="nov-hud__pill">{resolution}</span> : null}
        <button
          type="button"
          className="nov-icon-btn"
          style={{ width: 24, height: 24 }}
          onClick={onToggleExpanded}
          aria-label={expanded ? "Collapse statistics" : "Expand statistics"}
          aria-expanded={expanded}
        >
          {expanded ? <ChevronUp size={13} /> : <ChevronDown size={13} />}
        </button>
        <button
          type="button"
          className="nov-icon-btn"
          style={{ width: 24, height: 24 }}
          onClick={onClose}
          aria-label="Hide statistics"
        >
          <X size={13} />
        </button>
      </header>

      <div className="nov-hud__body">
        <div className="nov-hud__trio">
          <div className="nov-hud__cell">
            <div className="nov-hud__cell-label">Fps</div>
            <div
              className="nov-hud__cell-value"
              style={{ color: toneColor(fpsTone(streamFps)) }}
            >
              {streamFps ? formatInteger(streamFps) : "--"}
            </div>
          </div>
          <div className="nov-hud__cell">
            <div className="nov-hud__cell-label">Mbps</div>
            <div className="nov-hud__cell-value">{formatBitrateMbps(stats.bitrateKbps)}</div>
          </div>
          <div className="nov-hud__cell">
            <div className="nov-hud__cell-label">Ping</div>
            <div className="nov-hud__cell-value" style={{ color: toneColor(rttTone(stats.rttMs)) }}>
              {stats.rttMs && stats.rttMs > 0 ? formatInteger(stats.rttMs) : "--"}
              <small>ms</small>
            </div>
          </div>
        </div>

        <div className="nov-hud__spark" aria-hidden="true">
          <svg viewBox="0 0 292 42" preserveAspectRatio="none">
            <defs>
              <linearGradient id="nov-spark-line" x1="0" y1="0" x2="0" y2="1">
                <stop offset="0%" stopColor="var(--accent, #76ff3b)" stopOpacity="0.55" />
                <stop offset="100%" stopColor="var(--accent, #76ff3b)" stopOpacity="0" />
              </linearGradient>
            </defs>
            {sparkline.area ? <path d={sparkline.area} fill="url(#nov-spark-line)" /> : null}
            {sparkline.line ? (
              <path
                d={sparkline.line}
                fill="none"
                stroke="var(--accent, #76ff3b)"
                strokeWidth="1.6"
                strokeLinejoin="round"
                strokeLinecap="round"
              />
            ) : null}
          </svg>
        </div>

        <div className="nov-hud__rows">
          <div className="nov-hud__row">
            <span>
              <Wifi size={11} aria-hidden="true" /> Packet loss
            </span>
            <strong style={{ color: toneColor(lossTone(loss)) }}>{loss.toFixed(1)}%</strong>
          </div>
          <div className="nov-hud__meter" aria-hidden="true">
            <div
              className="nov-hud__meter-fill"
              style={{ width: `${Math.min(100, Math.max(2, 100 - loss * 8))}%` }}
            />
          </div>
        </div>

        {expanded ? (
          <div className="nov-hud__rows">
            <div className="nov-hud__row">
              <span>Codec</span>
              <strong>{stats.codec ? stats.codec.toUpperCase() : "--"}</strong>
            </div>
            <div className="nov-hud__row">
              <span>Decoder</span>
              <strong>{stats.hardwareAcceleration || "hardware"}</strong>
            </div>
            <div className="nov-hud__row">
              <span>Frames decoded</span>
              <strong>{formatInteger(stats.framesDecoded)}</strong>
            </div>
            <div className="nov-hud__row">
              <span>Frames dropped</span>
              <strong>{formatInteger(stats.framesDropped)}</strong>
            </div>
            <div className="nov-hud__row">
              <span>Bitrate target</span>
              <strong>
                {stats.targetBitrateKbps ? `${formatBitrateMbps(stats.targetBitrateKbps)} Mbps` : "--"}
              </strong>
            </div>
            <div className="nov-hud__row">
              <span>Transport</span>
              <strong>
                <Activity size={11} aria-hidden="true" />{" "}
                {session.transport === "webrtc" ? "WebRTC" : "NVST"}
              </strong>
            </div>
          </div>
        ) : null}
      </div>
    </section>
  );
}

function toneColor(tone: "good" | "warn" | "neutral"): string | undefined {
  if (tone === "good") return "var(--color-neon, #76ff3b)";
  if (tone === "warn") return "var(--color-azure, #00a3ff)";
  return undefined;
}

/**
 * Builds the bitrate sparkline path. Values are Mbps samples; the vertical
 * scale is pinned to the largest sample so small fluctuations stay visible.
 */
function buildSparkline(values: number[]): { line: string; area: string } {
  const samples = values.filter((value) => Number.isFinite(value) && value >= 0).slice(-48);
  if (samples.length < 2) return { line: "", area: "" };
  const width = 292;
  const height = 42;
  const max = Math.max(...samples, 1);
  const step = width / (samples.length - 1);
  const points = samples.map((value, index) => {
    const ratio = Math.min(1, value / max);
    return { x: index * step, y: height - 3 - ratio * (height - 8) };
  });
  const line = points
    .map((point, index) => `${index === 0 ? "M" : "L"}${point.x.toFixed(1)} ${point.y.toFixed(1)}`)
    .join(" ");
  const area = `${line} L${width} ${height} L0 ${height} Z`;
  return { line, area };
}
