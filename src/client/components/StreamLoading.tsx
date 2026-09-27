import React, { Fragment, useEffect, useRef, useState, type JSX } from "react";
import { AnimatePresence, m } from "motion/react";
import { Activity, AlertTriangle, Check, Clock, RotateCcw, X } from "lucide-react";
import { useTranslation } from "../i18n";
import { getStoreDisplayName, getStoreIconComponent } from "./GameCard";
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
import { LazyShaderAtmosphere } from "./LazyShaderAtmosphere";

export interface StreamLoadingProps {
  gameTitle: string; // e.g. "Cyberpunk 2077"
  gameCover?: string; // cover/key-art image URL (may be undefined)
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
 * Hairline progress across the top edge. Deliberately never reads 100% before
 * the stream actually takes over the screen, so the bar cannot lie about a
 * launch that then stalls.
 */
const STAGE_PROGRESS = [0.18, 0.48, 0.76, 1] as const;

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
      {digits.map((digit, i) => (
        <span className="qs-odometer-col" key={i}>
          <AnimatePresence mode="popLayout" initial={false}>
            <m.span
              key={digit}
              className="qs-odometer-digit"
              initial={{ y: "105%", opacity: 0 }}
              animate={{ y: "0%", opacity: 1 }}
              exit={{ y: "-105%", opacity: 0 }}
              transition={
                reduced ? { duration: 0.01 } : { type: "spring", stiffness: 520, damping: 40 }
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

export function StreamLoading(props: StreamLoadingProps): JSX.Element {
  const {
    gameTitle,
    gameCover,
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
  const [drops, setDrops] = useState<{ id: number; diff: number }[]>([]);
  const prevPosRef = useRef<number | undefined>(undefined);
  const dropIdRef = useRef(0);
  const reducedMotion = usePrefersReducedMotion();

  useEffect(() => {
    if (error) return;
    const id = setInterval(() => setElapsed((s) => s + 1), 1000);
    return () => clearInterval(id);
  }, [error]);

  // Celebrate queue movement with a floating "-N" indicator.
  useEffect(() => {
    const prev = prevPosRef.current;
    prevPosRef.current = queuePosition;
    if (prev === undefined || queuePosition === undefined || queuePosition >= prev) return;
    dropIdRef.current += 1;
    const id = dropIdRef.current;
    const diff = prev - queuePosition;
    setDrops((current) => [...current.slice(-2), { id, diff }]);
    window.setTimeout(() => {
      setDrops((current) => current.filter((drop) => drop.id !== id));
    }, 1100);
  }, [queuePosition]);

  const stageIndex = statusStageIndex(status);
  const stageProgress = error ? 1 : STAGE_PROGRESS[stageIndex];
  const storeLabel = platformStore ? getStoreDisplayName(platformStore) : undefined;
  const StoreIcon = platformStore ? getStoreIconComponent(platformStore) : null;
  const localTime = new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
  const titleWords = gameTitle.split(" ").filter(Boolean);
  const fallbackLetter = gameTitle?.trim()?.charAt(0)?.toUpperCase() || "?";

  const adsRequired = adState ? isSessionAdsRequired(adState) : false;
  const queuePaused = adState ? isSessionQueuePaused(adState) : false;
  const adMessage = adState ? getSessionAdMessage(adState) : null;
  const adCount = adState ? getSessionAdItems(adState).length : 0;
  const resolvedAdMediaUrl =
    activeAdMediaUrl || (activeAd ? getPreferredSessionAdMediaUrl(activeAd) : undefined);
  const showAdPlayer = Boolean(activeAd && resolvedAdMediaUrl);

  // The giant numeral only makes sense while actually holding a queue place.
  const hasPlace = status === "queue" && queuePosition != null;

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

  // With the numeral on stage the detail line leads; otherwise the status does.
  const primaryLine = hasPlace ? detailText : cozyText;
  const secondaryLine = hasPlace ? cozyText : detailText;

  const entranceTransition = reducedMotion
    ? { duration: 0.01 }
    : { duration: 0.55, ease: [0.22, 1, 0.36, 1] as const };
  const rise = (delay: number, y = 16) =>
    reducedMotion
      ? { initial: { opacity: 0 }, animate: { opacity: 1 }, transition: { duration: 0.01, delay } }
      : {
          initial: { opacity: 0, y },
          animate: { opacity: 1, y: 0 },
          transition: { duration: 0.6, delay, ease: [0.22, 1, 0.36, 1] as const },
        };

  const rootClasses = [
    "qs-root",
    error ? "qs-root--error" : "",
    reducedMotion ? "qs-root--static" : "",
  ]
    .filter(Boolean)
    .join(" ");

  return (
    <div className={rootClasses} data-stage={stageIndex}>
      <div className="qs-atmosphere" aria-hidden="true">
        <LazyShaderAtmosphere
          variant={status === "queue" ? "queue" : "connecting"}
          className="qs-shader"
        />
        <div
          className={`qs-backdrop${gameCover ? "" : " qs-backdrop--fallback"}`}
          style={gameCover ? { backgroundImage: `url(${gameCover})` } : undefined}
        >
          {!gameCover && <span className="qs-backdrop-letter">{fallbackLetter}</span>}
        </div>
        <div className="qs-aurora qs-aurora--a" />
        <div className="qs-aurora qs-aurora--b" />
        <div className="qs-spotlight" />
        <div className="qs-grid" />
        <div className="qs-sweep" />
        <div className="qs-vignette" />
        <div className="qs-grain" />
        {!error && !reducedMotion && hasPlace && (
          <m.div
            key={`tick-${queuePosition}`}
            className="qs-tickflash"
            initial={{ opacity: 0.6 }}
            animate={{ opacity: 0 }}
            transition={{ duration: 0.85, ease: "easeOut" }}
          />
        )}
        {!error && !reducedMotion && stageIndex > 0 && (
          <m.div
            key={`stage-${stageIndex}`}
            className="qs-stageflash"
            initial={{ opacity: 0.75 }}
            animate={{ opacity: 0 }}
            transition={{ duration: 1, ease: "easeOut" }}
          />
        )}
      </div>

      <div className="qs-progress" aria-hidden="true">
        <m.span
          className="qs-progress-fill"
          initial={{ width: "0%" }}
          animate={{ width: `${stageProgress * 100}%` }}
          transition={
            reducedMotion
              ? { duration: 0.01 }
              : { duration: 0.9, ease: [0.22, 1, 0.36, 1] }
          }
        />
      </div>

      <header className="qs-topbar">
        <div className="qs-nowpill">
          {!error && !reducedMotion ? (
            <span className="qs-eq" aria-hidden="true">
              <span /><span /><span /><span /><span />
            </span>
          ) : (
            <span className={`qs-nowdot${error ? " qs-nowdot--error" : ""}`} aria-hidden="true" />
          )}
          <span>{error ? t("streamLoading.labels.launchError") : t("streamLoading.labels.nowLoading")}</span>
        </div>
        <button
          type="button"
          className="qs-iconbtn"
          aria-label={t("streamLoading.actions.cancelLoading")}
          onClick={onCancel}
        >
          <X size={17} />
        </button>
      </header>

      <main className="qs-main">
        {error ? (
          <m.div
            className="qs-error"
            role="alert"
            initial={{ opacity: 0, y: reducedMotion ? 0 : 18 }}
            animate={{ opacity: 1, y: 0 }}
            transition={entranceTransition}
          >
            <span className="qs-error-ring" aria-hidden="true">
              <AlertTriangle size={26} />
            </span>
            <div className="qs-error-eyebrow">{t("streamLoading.labels.launchError")}</div>
            <h1 className="qs-error-title">
              {error.title || t("streamLoading.status.gameLaunchFailed")}
            </h1>
            <p className="qs-error-desc">{error.description}</p>
            {error.code && <code className="qs-error-code">{error.code}</code>}
            <div className="qs-error-actions">
              {error.actionLabel && onErrorAction && (
                <button
                  type="button"
                  className="qs-btn qs-btn-primary"
                  onClick={onErrorAction}
                  autoFocus
                >
                  <RotateCcw size={15} />
                  {error.actionLabel}
                </button>
              )}
              <button
                type="button"
                className="qs-btn qs-btn-ghost"
                onClick={onCancel}
                autoFocus={!error.actionLabel || !onErrorAction}
              >
                {t("app.actions.close")}
              </button>
            </div>
          </m.div>
        ) : (
          <div className="qs-stack">
            <m.div className="qs-poster-wrap" {...rise(0.05, 22)}>
              <div className="qs-poster-inner">
                <span className="qs-poster-glow" aria-hidden="true" />
                <span className="qs-bracket qs-bracket--tl" aria-hidden="true" />
                <span className="qs-bracket qs-bracket--tr" aria-hidden="true" />
                <span className="qs-bracket qs-bracket--bl" aria-hidden="true" />
                <span className="qs-bracket qs-bracket--br" aria-hidden="true" />
                <div
                  className={`qs-poster${gameCover ? "" : " qs-poster--fallback"}`}
                  style={gameCover ? { backgroundImage: `url(${gameCover})` } : undefined}
                >
                  {!gameCover && <span className="qs-poster-letter">{fallbackLetter}</span>}
                  <span className="qs-poster-sheen" aria-hidden="true" />
                  <span className="qs-poster-scan" aria-hidden="true" />
                </div>
              </div>
            </m.div>

            <div className="qs-masthead">
              {storeLabel && (
                <m.span className="qs-store-chip" {...rise(0.16, 12)}>
                  {StoreIcon && <StoreIcon />}
                  {storeLabel}
                </m.span>
              )}
              <h1 className="qs-title" title={gameTitle}>
                {titleWords.map((word, i) => (
                  <m.span
                    key={`${word}-${i}`}
                    className="qs-title-word"
                    initial={{ opacity: 0, y: reducedMotion ? 0 : "0.5em" }}
                    animate={{ opacity: 1, y: 0 }}
                    transition={{
                      duration: reducedMotion ? 0.01 : 0.6,
                      delay: reducedMotion ? 0 : 0.24 + i * 0.07,
                      ease: [0.22, 1, 0.36, 1],
                    }}
                  >
                    {word}
                    {i < titleWords.length - 1 ? "\u00A0" : ""}
                  </m.span>
                ))}
              </h1>
            </div>

            <div className="qs-hero" role="status" aria-live="polite">
              <AnimatePresence mode="wait" initial={false}>
                {hasPlace ? (
                  <m.div
                    key="place"
                    className="qs-position"
                    initial={{ opacity: 0, scale: reducedMotion ? 1 : 0.92 }}
                    animate={{ opacity: 1, scale: 1 }}
                    exit={{ opacity: 0, scale: reducedMotion ? 1 : 1.02 }}
                    transition={{ duration: 0.4, ease: [0.22, 1, 0.36, 1] }}
                  >
                    <span className="qs-position-hash" aria-hidden="true">
                      #
                    </span>
                    <OdometerNumber value={queuePosition as number} reduced={reducedMotion} className="qs-pnum" />
                    <span className="qs-drops" aria-hidden="true">
                      <AnimatePresence>
                        {drops.map((drop) => (
                          <m.span
                            key={drop.id}
                            className="qs-drop"
                            initial={{ opacity: 0, y: 12, scale: 0.85 }}
                            animate={{ opacity: 1, y: -10, scale: 1 }}
                            exit={{ opacity: 0, y: -42 }}
                            transition={{ duration: 1, ease: "easeOut" }}
                          >
                            −{drop.diff}
                          </m.span>
                        ))}
                      </AnimatePresence>
                    </span>
                  </m.div>
                ) : (
                  <m.p
                    key={`status-${status}`}
                    className="qs-statusline"
                    initial={{ opacity: 0, y: reducedMotion ? 0 : 10 }}
                    animate={{ opacity: 1, y: 0 }}
                    exit={{ opacity: 0, y: reducedMotion ? 0 : -10 }}
                    transition={{ duration: reducedMotion ? 0.01 : 0.3 }}
                  >
                    {statusText}
                  </m.p>
                )}
              </AnimatePresence>
              {hasPlace && (
                <p className="qs-herolabel">{t("streamLoading.hero.placeInQueue")}</p>
              )}
              <span className="qs-sr-only">{statusText}</span>
            </div>

            <m.div className="qs-rail-wrap" {...rise(0.34, 14)}>
              <div
                className="qs-rail"
                role="group"
                aria-label={t("streamLoading.labels.launchProgress")}
              >
                {STAGE_KEYS.map((key, i) => (
                  <Fragment key={key}>
                    {i > 0 && (
                      <span
                        className={`qs-rail-line${i <= stageIndex ? " qs-rail-line--on" : ""}`}
                        aria-hidden="true"
                      >
                        <i />
                      </span>
                    )}
                    <div
                      className={[
                        "qs-rail-step",
                        i === stageIndex ? "qs-rail-step--active" : "",
                        i < stageIndex ? "qs-rail-step--done" : "",
                      ]
                        .filter(Boolean)
                        .join(" ")}
                    >
                      <span className="qs-rail-dot" aria-hidden="true">
                        {i < stageIndex && <Check size={9} strokeWidth={4} />}
                        {i === stageIndex && <span className="qs-rail-pulse" />}
                      </span>
                      <span className="qs-rail-name">{t(`streamLoading.steps.${key}`)}</span>
                    </div>
                  </Fragment>
                ))}
              </div>
            </m.div>

            <div className="qs-copy">
              <AnimatePresence mode="wait" initial={false}>
                <m.p
                  key={`${status}-${primaryLine}`}
                  className="qs-line"
                  initial={{ opacity: 0, y: reducedMotion ? 0 : 8 }}
                  animate={{ opacity: 1, y: 0 }}
                  exit={{ opacity: 0, y: reducedMotion ? 0 : -8 }}
                  transition={{ duration: reducedMotion ? 0.01 : 0.28 }}
                >
                  {primaryLine}
                </m.p>
              </AnimatePresence>
              {secondaryLine && <p className="qs-sub">{secondaryLine}</p>}
            </div>

            <m.div className="qs-chips" {...rise(0.42, 12)}>
              {status === "queue" && (
                <span className="qs-chip qs-chip--accent">
                  <Clock size={12} aria-hidden="true" />
                  <span className="qs-chip-k">{t("streamLoading.hero.estWait")}</span>
                  <span className="qs-chip-v">{estimatedWait || t("streamLoading.telemetry.calculating")}</span>
                </span>
              )}
              <span className="qs-chip">
                <Activity size={12} aria-hidden="true" />
                <span className="qs-chip-k">{t("streamLoading.telemetry.elapsed")}</span>
                <span className="qs-chip-v">{formatElapsed(elapsed)}</span>
              </span>
              <span className="qs-chip">
                <span className="qs-chip-dot" aria-hidden="true" />
                <span className="qs-chip-k">{t("streamLoading.telemetry.session")}</span>
                <span className="qs-chip-v">{localTime}</span>
              </span>
              {hasPlace && queuePosition > 1 && (
                <span className="qs-chip qs-chip--plain">
                  {t("streamLoading.hero.aheadOfYou", { count: queuePosition - 1 })}
                </span>
              )}
            </m.div>

            {diagnosticLine && <p className="qs-diagnostic">{diagnosticLine}</p>}

            {(adsRequired || queuePaused || showAdPlayer) && (
              <div className="qs-adzone">
                <div className="qs-adzone-label">{t("streamLoading.labels.adQueue")}</div>

                {queuePaused && (
                  <div className="qs-ad-paused">
                    <span className="qs-rule" aria-hidden="true" />
                    <p className="qs-ad-paused-title">{t("streamLoading.status.queuePaused")}</p>
                    <p className="qs-ad-paused-sub">{t("streamLoading.ads.resumeToStayInQueue")}</p>
                  </div>
                )}

                {!queuePaused && adsRequired && (
                  <p className="qs-ad-message">
                    {adMessage || t("streamLoading.ads.playbackRequired")}
                    {adCount > 0
                      ? ` ${t("streamLoading.ads.availableForProgression", { count: adCount })}`
                      : ""}
                  </p>
                )}

                {showAdPlayer && activeAd && (
                  <div className="qs-ad-player">
                    <div className="qs-ad-player-head">
                      <span className="qs-ad-tag">{t("streamLoading.ads.advertisement")}</span>
                      <span className="qs-ad-title">{activeAd.title}</span>
                      <span className="qs-ad-live">{t("streamLoading.ads.advertisementInProgress")}</span>
                    </div>
                    <QueueAdPreview
                      ref={adPreviewRef}
                      mediaUrl={resolvedAdMediaUrl as string}
                      title={activeAd.title}
                      onPlaybackEvent={(event) => onAdPlaybackEvent?.(event, activeAd.adId)}
                    />
                  </div>
                )}
              </div>
            )}

            <m.button
              type="button"
              className="qs-btn qs-btn-ghost qs-btn--cancel"
              onClick={onCancel}
              autoFocus
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
              transition={{ duration: reducedMotion ? 0.01 : 0.5, delay: reducedMotion ? 0 : 0.5 }}
            >
              <X size={15} aria-hidden="true" />
              {t("app.actions.cancel")}
            </m.button>
          </div>
        )}
      </main>
    </div>
  );
}
