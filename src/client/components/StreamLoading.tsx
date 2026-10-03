import React, { useEffect, useMemo, useRef, useState, type JSX } from "react";
import { AnimatePresence, m } from "motion/react";
import { Check, RotateCcw } from "lucide-react";
import { useTranslation } from "../i18n";
import { getStoreDisplayName } from "./GameCard";
import {
  isSessionAdsRequired,
  isSessionQueuePaused,
  getSessionAdMessage,
  getSessionAdItems,
  getPreferredSessionAdMediaUrl,
  type SessionAdState,
  type SessionAdInfo,
} from "@shared/gfn";
import {
  QueueAdPreview,
  type QueueAdPlaybackEvent,
  type QueueAdPreviewHandle,
} from "./QueueAdPreview";

export interface StreamLoadingProps {
  gameTitle: string; // e.g. "Cyberpunk 2077"
  gameCover?: string; // portrait box-art URL (may be undefined)
  gameHero?: string; // wide hero/key-art URL — preferred for the art band (may be undefined)
  platformStore?: string; // raw store id, e.g. "steam" — display via getStoreDisplayName()
  status: "queue" | "setup" | "starting" | "connecting";
  queuePosition?: number; // live queue number (may be undefined)
  diagnosticLine?: string | null; // raw live diagnostic text (may be null)
  estimatedWait?: string; // pre-formatted, e.g. "~3 min" (may be undefined)
  adState?: SessionAdState; // opaque — use helpers below only
  activeAd?: SessionAdInfo; // opaque except .adId and .title
  activeAdMediaUrl?: string; // resolved ad video URL (may be undefined)
  error?: { title: string; description: string; code?: string; actionLabel?: string };
  onAdPlaybackEvent?: (event: QueueAdPlaybackEvent, adId: string) => void;
  adPreviewRef?: React.Ref<QueueAdPreviewHandle>;
  onErrorAction?: () => void; // retry handler
  onCancel: () => void; // cancel/close handler
}

const STAGE_KEYS = ["queue", "setup", "connect", "ready"] as const;

/**
 * Fill of the launch bar, taken at the centre of each step so the bar ends
 * exactly under the dot it belongs to. It never reaches 100% — the stream view
 * replaces this screen once the session is actually up.
 */
const STAGE_PROGRESS = [0.125, 0.375, 0.625, 0.875] as const;

/** Voice lines that rotate under the body copy (8s cadence). */
const EDITORIAL_KEYS = [
  "streamLoading.editorial.keepWindowOpen",
  "streamLoading.editorial.noInstalls",
  "streamLoading.editorial.savesCarryOver",
] as const;

const EDITORIAL_INTERVAL_MS = 8000;

/** How many queue movements the movement trace remembers. */
const MOVEMENT_SAMPLES = 14;

/** Launch stage the current stream status maps onto (index into STAGE_KEYS). */
function statusStageIndex(status: StreamLoadingProps["status"]): number {
  switch (status) {
    case "queue":
      return 0;
    case "setup":
      return 1;
    case "starting":
      return 2;
    case "connecting":
      return 2;
    default:
      return 0;
  }
}

function formatElapsed(totalSeconds: number): string {
  const mins = Math.floor(totalSeconds / 60).toString().padStart(2, "0");
  const secs = Math.floor(totalSeconds % 60).toString().padStart(2, "0");
  return `${mins}:${secs}`;
}

/** ISO 8601 duration for the <time> element, e.g. "PT02M07S". */
function elapsedDuration(totalSeconds: number): string {
  const mins = Math.floor(totalSeconds / 60);
  const secs = Math.floor(totalSeconds % 60);
  return `PT${mins}M${secs}S`;
}

/**
 * Height of one movement bar, as a percentage of the trace track. The largest
 * jump in the window fills the track; everything else is scaled against it, and
 * a floor keeps a single-place move from disappearing.
 */
export function movementBarPercent(diff: number, largest: number): number {
  const safeLargest = Math.max(1, largest);
  const scaled = Math.round((Math.max(0, diff) / safeLargest) * 100);
  return Math.min(100, Math.max(12, scaled));
}

function usePrefersReducedMotion(): boolean {
  const [reduced, setReduced] = useState<boolean>(() => {
    if (typeof window === "undefined" || !window.matchMedia) return false;
    return window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  });
  useEffect(() => {
    if (typeof window === "undefined" || !window.matchMedia) return;
    const mq = window.matchMedia("(prefers-reduced-motion: reduce)");
    const handler = () => setReduced(mq.matches);
    mq.addEventListener?.("change", handler);
    return () => mq.removeEventListener?.("change", handler);
  }, []);
  return reduced;
}

/** Per-digit roll counter: each column keeps its own digit and slides on change. */
function OdometerNumber({
  value,
  reduced,
  className,
}: {
  value: number;
  reduced: boolean;
  className?: string;
}): JSX.Element {
  const digits = String(Math.max(0, Math.trunc(value))).split("");
  const classes = ["qs-odometer", className].filter(Boolean).join(" ");
  return (
    <span className={classes} aria-hidden="true">
      {digits.map((digit, index) => (
        <span className="qs-odometer-col" key={index}>
          <AnimatePresence mode="popLayout" initial={false}>
            <m.span
              key={digit}
              className="qs-odometer-digit"
              initial={{ y: "108%", opacity: 0 }}
              animate={{ y: "0%", opacity: 1 }}
              exit={{ y: "-108%", opacity: 0 }}
              transition={
                reduced ? { duration: 0.01 } : { duration: 0.3, ease: [0.34, 1.56, 0.64, 1] }
              }
            >
              {digit}
            </m.span>
          </AnimatePresence>
        </span>
      ))}
    </span>
  );
}

/**
 * Queue screen — a magazine spread with a console in it.
 *
 * Two honest zones: an art band on the left (4fr) carrying the game's artwork
 * as raw material — full bleed, graded, lit from within, and never carrying
 * panel text — and a black information panel on the right (3fr) that reads
 * like flight hardware. A neon seam is the only join between them.
 *
 * The panel is one focal point (the live position, glowing, rolling digit by
 * digit, with the queue's movement traced beside it) surrounded by small,
 * still type: store, title, stats, the launch rail, one informative line and
 * one voice line. Ads never get a section of their own — when one plays it
 * takes over the art band, and when one is merely pending the body line says
 * so, because that is the line whose job is explaining what is happening.
 */
export function StreamLoading(props: StreamLoadingProps): JSX.Element {
  const {
    gameTitle,
    gameCover,
    gameHero,
    platformStore,
    status,
    queuePosition,
    diagnosticLine,
    estimatedWait,
    adState,
    activeAd,
    activeAdMediaUrl,
    error,
    onAdPlaybackEvent,
    adPreviewRef,
    onErrorAction,
    onCancel,
  } = props;

  const { t } = useTranslation();
  const [elapsed, setElapsed] = useState(0);
  const [editorialIndex, setEditorialIndex] = useState(0);
  const [drops, setDrops] = useState<{ id: number; diff: number }[]>([]);
  const [movement, setMovement] = useState<{ id: number; diff: number }[]>([]);
  const prevPosRef = useRef<number | undefined>(undefined);
  const dropIdRef = useRef(0);
  const rootRef = useRef<HTMLDivElement | null>(null);
  const reducedMotion = usePrefersReducedMotion();

  useEffect(() => {
    if (error) return;
    const id = setInterval(() => setElapsed((seconds) => seconds + 1), 1000);
    return () => clearInterval(id);
  }, [error]);

  // Queue movement is the one event worth interrupting for: float a "−N" badge
  // and keep the jump in the movement trace.
  useEffect(() => {
    const previous = prevPosRef.current;
    prevPosRef.current = queuePosition;
    if (previous === undefined || queuePosition === undefined || queuePosition >= previous) return;
    dropIdRef.current += 1;
    const id = dropIdRef.current;
    const diff = previous - queuePosition;
    setDrops((current) => [...current.slice(-2), { id, diff }]);
    setMovement((current) => [...current, { id, diff }].slice(-MOVEMENT_SAMPLES));
    window.setTimeout(() => {
      setDrops((current) => current.filter((drop) => drop.id !== id));
    }, 1200);
  }, [queuePosition]);

  // Escape is the keyboard equivalent of the cancel button, but only while this
  // screen (or nothing at all) holds focus — never steal it from the app below.
  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent): void => {
      if (event.key !== "Escape" || event.defaultPrevented) return;
      const active = document.activeElement;
      if (active && active !== document.body && !rootRef.current?.contains(active)) return;
      event.preventDefault();
      onCancel();
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [onCancel]);

  const stageIndex = statusStageIndex(status);
  const stageProgress = STAGE_PROGRESS[stageIndex];
  const stageLabel = t(`streamLoading.steps.${STAGE_KEYS[stageIndex]}`);
  const storeLabel = platformStore ? getStoreDisplayName(platformStore) : undefined;

  // Art priority: wide hero → portrait box art → 16:9 screenshot (passed in as
  // either of the two above by the caller) → intentional no-art ground.
  const art = gameHero || gameCover;
  const artKind = gameHero ? "hero" : gameCover ? "cover" : "none";

  const adsRequired = adState ? isSessionAdsRequired(adState) : false;
  const queuePaused = adState ? isSessionQueuePaused(adState) : false;
  const adMessage = adState ? getSessionAdMessage(adState) : null;
  const adCount = adState ? getSessionAdItems(adState).length : 0;
  const resolvedAdMediaUrl =
    activeAdMediaUrl || (activeAd ? getPreferredSessionAdMediaUrl(activeAd) : undefined);
  const showAdPlayer = Boolean(activeAd && resolvedAdMediaUrl);

  // The numeral only makes sense while the session actually holds a place.
  const hasPlace = status === "queue" && queuePosition != null;
  const aheadCount = hasPlace ? Math.max(0, (queuePosition as number) - 1) : undefined;
  const waitKnown = Boolean(estimatedWait);
  const hasRetry = Boolean(onErrorAction);

  const movementTotal = movement.reduce((sum, sample) => sum + sample.diff, 0);
  const movementLargest = movement.reduce((max, sample) => Math.max(max, sample.diff), 1);

  const statusText = (() => {
    switch (status) {
      case "queue":
        return queuePosition != null
          ? t("streamLoading.status.positionInQueue", { position: queuePosition })
          : t("streamLoading.status.waitingInQueue");
      case "setup":
        return t("streamLoading.status.settingUpRig");
      case "starting":
        return t("streamLoading.status.startingStream");
      case "connecting":
        return t("streamLoading.status.connectingToServer");
      default:
        return t("streamLoading.status.loading");
    }
  })();

  const cozyText = (() => {
    switch (status) {
      case "queue":
        return t("streamLoading.cozy.queue");
      case "setup":
        return t("streamLoading.cozy.setup");
      case "starting":
        return t("streamLoading.cozy.starting");
      case "connecting":
        return t("streamLoading.cozy.connecting");
      default:
        return "";
    }
  })();

  const detailText = (() => {
    switch (status) {
      case "queue":
        return queuePosition != null
          ? t("streamLoading.detail.queueMoving", { position: queuePosition })
          : t("streamLoading.detail.findingCapacity");
      case "setup":
        return t("streamLoading.detail.rigReserved");
      case "starting":
        return t("streamLoading.detail.servicesStarting");
      case "connecting":
        return t("streamLoading.detail.mediaHandshake");
      default:
        return "";
    }
  })();

  /**
   * The informative line. Ad states are folded in here rather than getting a
   * section of their own: when the queue is held for an ad, or an ad is still
   * pending, that *is* what is happening right now.
   */
  const bodyLine = (() => {
    if (queuePaused) {
      return `${t("streamLoading.status.queuePaused")} ${t("streamLoading.ads.resumeToStayInQueue")}`;
    }
    if (adsRequired && !showAdPlayer) {
      const base = adMessage || t("streamLoading.ads.playbackRequired");
      return adCount > 0
        ? `${base} ${t("streamLoading.ads.availableForProgression", { count: adCount })}`
        : base;
    }
    return detailText;
  })();

  /** The status word in the topbar: short, still, sentence case. */
  const topbarStatus = (() => {
    if (error) return t("streamLoading.topbar.error");
    switch (status) {
      case "queue":
        return t("streamLoading.hero.inQueue");
      case "setup":
        return t("streamLoading.topbar.settingUp");
      case "starting":
        return t("streamLoading.topbar.almostReady");
      case "connecting":
        return t("streamLoading.topbar.connecting");
      default:
        return t("streamLoading.topbar.settingUp");
    }
  })();

  // Status-specific reassurance first, then the rotating tips.
  const editorialLines = useMemo(
    () => [cozyText, ...EDITORIAL_KEYS.map((key) => t(key))].filter((line) => line.length > 0),
    [cozyText, t],
  );

  useEffect(() => {
    if (error || editorialLines.length < 2) return;
    const id = setInterval(() => {
      setEditorialIndex((index) => (index + 1) % editorialLines.length);
    }, EDITORIAL_INTERVAL_MS);
    return () => clearInterval(id);
  }, [error, editorialLines.length]);

  const editorialLine = editorialLines[editorialIndex % Math.max(1, editorialLines.length)] ?? "";

  return (
    <div
      className="qs-root"
      data-stage={stageIndex}
      data-error={error ? "true" : undefined}
      data-motion={reducedMotion ? "reduced" : undefined}
      data-art={art ? "on" : "off"}
      data-ad={showAdPlayer ? "playing" : undefined}
      ref={rootRef}
    >
      {/* Row 1 — persistent chrome: live status pill, elapsed clock, cancel. */}
      <header className="qs-topbar">
        <div className="qs-topbar-left">
          <span className={error ? "qs-status-pill qs-status-pill--error" : "qs-status-pill"}>
            <span className="qs-dot" aria-hidden="true" />
            <span className="qs-status-text">{topbarStatus}</span>
          </span>
          <span className="qs-sep" aria-hidden="true">
            ·
          </span>
          <time
            className="qs-timer"
            dateTime={elapsedDuration(elapsed)}
            title={t("streamLoading.telemetry.elapsed")}
          >
            {formatElapsed(elapsed)}
          </time>
        </div>
        <button
          type="button"
          className="qs-btn-ghost"
          onClick={onCancel}
          autoFocus={!error}
          aria-label={t("streamLoading.actions.cancelLoading")}
        >
          {t("app.actions.cancel")}
        </button>
      </header>

      {/* Row 2 — the split: art band (4fr) and information panel (3fr). */}
      <div className="qs-main">
        <div
          className="qs-art"
          data-kind={artKind}
          aria-hidden={showAdPlayer ? undefined : "true"}
        >
          {art ? (
            <div className="qs-art-image" style={{ backgroundImage: `url(${art})` }} />
          ) : (
            <span className="qs-art-letter">
              {(gameTitle.trim().charAt(0) || "O").toUpperCase()}
            </span>
          )}
          <div className="qs-art-grade" />
          <div className="qs-art-bloom" />

          {/* An ad never gets a panel of its own: it takes the whole band. */}
          {showAdPlayer && activeAd && (
            <div className="qs-art-takeover">
              <span className="qs-art-adtag">{t("streamLoading.ads.adTag")}</span>
              <QueueAdPreview
                ref={adPreviewRef}
                mediaUrl={resolvedAdMediaUrl as string}
                title={activeAd.title}
                onPlaybackEvent={(event) => onAdPlaybackEvent?.(event, activeAd.adId)}
              />
            </div>
          )}
        </div>

        <div className="qs-panel">
          <div className="qs-panel-grid" aria-hidden="true" />
          <div className="qs-panel-scan" aria-hidden="true" />
          <div className="qs-panel-glow" aria-hidden="true" />
          <div className="qs-panel-sweep" aria-hidden="true" />
          <div className="qs-panel-edge" aria-hidden="true" />
          <div className="qs-panel-inner">
            {/* Masthead: provider, then the title. */}
            <div className="qs-meta">
              {storeLabel && (
                <div className="qs-provider">
                  <span className="qs-provider-badge" aria-hidden="true">
                    {storeLabel.charAt(0).toUpperCase()}
                  </span>
                  <span className="qs-provider-name">{storeLabel}</span>
                </div>
              )}
              <h1 className="qs-title" title={gameTitle}>
                {gameTitle}
              </h1>
            </div>

            {error ? (
              <div className="qs-plate qs-plate--azure qs-error" role="alert">
                <p className="qs-error-eyebrow">{t("streamLoading.labels.launchError")}</p>
                <h2 className="qs-error-title">
                  {error.title || t("streamLoading.status.gameLaunchFailed")}
                </h2>
                <p className="qs-error-desc">{error.description}</p>
                {error.code && (
                  <p className="qs-code-chip">
                    <span className="qs-sr-only">{t("streamLoading.error.codeLabel")}: </span>
                    {error.code}
                  </p>
                )}
                <div className="qs-error-actions">
                  {hasRetry && (
                    <button
                      type="button"
                      className="qs-btn-primary"
                      onClick={onErrorAction}
                      autoFocus
                    >
                      <RotateCcw size={16} aria-hidden="true" />
                      {error.actionLabel || t("streamLoading.actions.tryAgain")}
                    </button>
                  )}
                  <button
                    type="button"
                    className="qs-btn-ghost"
                    onClick={onCancel}
                    autoFocus={!hasRetry}
                  >
                    {t("app.actions.cancel")}
                  </button>
                </div>
              </div>
            ) : (
              <>
                {/* The console: where you are, and how fast it is moving. */}
                <section className="qs-plate" role="status" aria-live="polite">
                  <div className="qs-hero-top">
                    <p className="qs-hero-label">
                      {hasPlace
                        ? t("streamLoading.hero.yourPosition")
                        : t("streamLoading.hero.currentStage")}
                    </p>
                    <div className="qs-hero-value">
                      <span className="qs-live-dot" aria-hidden="true" />
                      {hasPlace ? (
                        <span className="qs-numeral">
                          <OdometerNumber value={queuePosition as number} reduced={reducedMotion} />
                          <span className="qs-drops" aria-hidden="true">
                            <AnimatePresence>
                              {drops.map((drop) => (
                                <m.span
                                  key={drop.id}
                                  className="qs-drop"
                                  initial={{ opacity: 0, y: reducedMotion ? 0 : 8 }}
                                  animate={{ opacity: 1, y: reducedMotion ? 0 : -6 }}
                                  exit={{ opacity: 0, y: reducedMotion ? 0 : -22 }}
                                  transition={{ duration: 0.6, ease: [0.22, 0.61, 0.36, 1] }}
                                >
                                  −{drop.diff}
                                </m.span>
                              ))}
                            </AnimatePresence>
                          </span>
                        </span>
                      ) : (
                        <span className="qs-stage-word">{stageLabel}</span>
                      )}
                    </div>
                  </div>

                  <ul className="qs-hud">
                    <li className={waitKnown ? "qs-cell" : "qs-cell qs-cell--muted"}>
                      <span className="qs-cell-label">
                        {t("streamLoading.hero.estimatedWait")}
                      </span>
                      <span className="qs-cell-value">
                        {estimatedWait || t("streamLoading.telemetry.calculating")}
                      </span>
                    </li>
                    <li
                      className={
                        aheadCount === undefined
                          ? "qs-cell qs-cell--muted"
                          : aheadCount === 0
                            ? "qs-cell qs-cell--next"
                            : "qs-cell"
                      }
                    >
                      <span className="qs-cell-label">
                        {t("streamLoading.hero.playersAhead")}
                      </span>
                      <span className="qs-cell-value">
                        {aheadCount === undefined
                          ? t("streamLoading.hero.valueUnknown")
                          : aheadCount === 0
                            ? t("streamLoading.hero.youreNext")
                            : aheadCount}
                      </span>
                    </li>
                  </ul>

                  {/* Movement trace: real jumps only, so it starts empty. */}
                  {movement.length > 0 && (
                    <div className="qs-movement">
                      <span className="qs-movement-label">
                        {t("streamLoading.movement.label")}
                      </span>
                      <span className="qs-movement-track" aria-hidden="true">
                        {movement.map((sample, index) => (
                          <span
                            key={sample.id}
                            className={
                              index === movement.length - 1
                                ? "qs-movement-bar qs-movement-bar--live"
                                : "qs-movement-bar"
                            }
                            style={{
                              height: `${movementBarPercent(sample.diff, movementLargest)}%`,
                            }}
                          />
                        ))}
                      </span>
                      <span className="qs-movement-value">
                        {t("streamLoading.movement.gained", { count: movementTotal })}{" "}
                        {t("streamLoading.movement.updates", { count: movement.length })}
                      </span>
                    </div>
                  )}

                  <span className="qs-sr-only">{statusText}</span>
                </section>

                {/* Launch progress: an energy rail with four stops. */}
                <div
                  className="qs-progress"
                  role="progressbar"
                  aria-label={t("streamLoading.labels.launchProgress")}
                  aria-valuemin={0}
                  aria-valuemax={100}
                  aria-valuenow={Math.floor(stageProgress * 100)}
                  aria-valuetext={t("streamLoading.progress.ariaText", {
                    current: stageIndex + 1,
                    total: STAGE_KEYS.length,
                    label: stageLabel,
                  })}
                >
                  <ol className="qs-segments">
                    {STAGE_KEYS.map((key, index) => (
                      <li
                        key={key}
                        className="qs-segment"
                        data-state={
                          index === stageIndex ? "active" : index < stageIndex ? "done" : "next"
                        }
                        aria-current={index === stageIndex ? "step" : undefined}
                      >
                        <span className="qs-segment-index" aria-hidden="true">
                          {index < stageIndex ? <Check size={13} strokeWidth={3.5} /> : index + 1}
                        </span>
                        <span className="qs-segment-label">{t(`streamLoading.steps.${key}`)}</span>
                      </li>
                    ))}
                  </ol>
                  <div className="qs-progress-track">
                    <m.span
                      className="qs-progress-fill"
                      initial={{ width: "0%" }}
                      animate={{ width: `${stageProgress * 100}%` }}
                      transition={
                        reducedMotion
                          ? { duration: 0.01 }
                          : { duration: 0.6, ease: [0.22, 0.61, 0.36, 1] }
                      }
                    />
                  </div>
                </div>

                {/* Supporting copy: one informative line, one voice line. */}
                <div className="qs-copy">
                  <p className="qs-body">{bodyLine}</p>
                  {editorialLine && (
                    <div className="qs-editorial-block">
                      <div className="qs-editorial-rule" aria-hidden="true" />
                      <AnimatePresence mode="wait" initial={false}>
                        <m.p
                          key={editorialLine}
                          className="qs-editorial"
                          initial={{ opacity: 0 }}
                          animate={{ opacity: 1 }}
                          exit={{ opacity: 0 }}
                          transition={
                            reducedMotion
                              ? { duration: 0.01 }
                              : { duration: 0.2, ease: [0.22, 0.61, 0.36, 1] }
                          }
                        >
                          {editorialLine}
                        </m.p>
                      </AnimatePresence>
                    </div>
                  )}
                </div>
              </>
            )}
          </div>
        </div>
      </div>

      {/* Row 3 — telemetry: one clamped line, full text on hover. */}
      {diagnosticLine && (
        <footer className="qs-footer">
          <span className="qs-diagnostics" title={diagnosticLine}>
            {diagnosticLine}
          </span>
        </footer>
      )}
    </div>
  );
}
