import React, { useEffect, useRef, useState, type JSX } from "react";
import { AnimatePresence, m } from "motion/react";
import { X, AlertTriangle, RotateCcw, Radio } from "lucide-react";
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
const SLICE_COUNT = 10;

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

function OdometerNumber({ value, reduced }: { value: number; reduced: boolean }): JSX.Element {
  const digits = String(value).split("");
  return (
    <span className="qs-odometer" aria-label={String(value)}>
      {digits.map((digit, i) => (
        <span className="qs-odometer-col" key={i} aria-hidden="true">
          <AnimatePresence mode="popLayout" initial={false}>
            <m.span
              key={digit}
              className="qs-odometer-digit"
              initial={{ y: "110%", opacity: 0 }}
              animate={{ y: "0%", opacity: 1 }}
              exit={{ y: "-110%", opacity: 0 }}
              transition={
                reduced
                  ? { duration: 0.01 }
                  : { type: "spring", stiffness: 550, damping: 42 }
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
    }, 1000);
  }, [queuePosition]);

  const stageIndex = statusStageIndex(status);
  const stageProgress = Math.min(1, (stageIndex + 0.5) / STAGE_KEYS.length);
  const storeLabel = platformStore ? getStoreDisplayName(platformStore) : undefined;
  const localTime = new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
  const titleWords = gameTitle.split(" ").filter(Boolean);

  const adsRequired = adState ? isSessionAdsRequired(adState) : false;
  const queuePaused = adState ? isSessionQueuePaused(adState) : false;
  const adMessage = adState ? getSessionAdMessage(adState) : null;
  const adCount = adState ? getSessionAdItems(adState).length : 0;
  const resolvedAdMediaUrl =
    activeAdMediaUrl || (activeAd ? getPreferredSessionAdMediaUrl(activeAd) : undefined);
  const showAdPlayer = Boolean(activeAd && resolvedAdMediaUrl);

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

  const slices = Array.from({ length: SLICE_COUNT });
  const fallbackLetter = gameTitle?.trim()?.charAt(0)?.toUpperCase() || "?";

  const entranceTransition = reducedMotion
    ? { duration: 0.01 }
    : { duration: 0.5, ease: [0.22, 1, 0.36, 1] as const };

  return (
    <div className={`qs-root${error ? " qs-root-error" : ""}`}>
      <div className="qs-atmosphere" aria-hidden="true">
        <LazyShaderAtmosphere
          variant={status === "queue" ? "queue" : "connecting"}
          className="qs-shader"
        />
        <div className={`qs-vault qs-vault-s${stageIndex} ${gameCover ? "" : "qs-vault-fallback"} ${reducedMotion ? "qs-no-motion" : ""}`}>
          {!gameCover && <div className="qs-vault-letter">{fallbackLetter}</div>}
          {slices.map((_, i) => (
            <m.div
              key={i}
              className="qs-vault-slice"
              initial={reducedMotion ? { opacity: 0 } : { opacity: 0, clipPath: "inset(0 0 100% 0)" }}
              animate={{ opacity: 1, clipPath: "inset(0 0 0% 0)" }}
              transition={{ duration: 0.7, delay: 0.15 + i * 0.05, ease: [0.22, 1, 0.36, 1] }}
              style={
                gameCover
                  ? {
                      backgroundImage: `url(${gameCover})`,
                      backgroundPositionX: `${(i / (SLICE_COUNT - 1)) * 100}%`,
                      animationDelay: `${i * 0.12}s`,
                    }
                  : { animationDelay: `${i * 0.12}s` }
              }
            />
          ))}
        </div>
        <div className="qs-aurora" />
        <div className="qs-scrim" />
        <div className="qs-grain" />
        {!error && !reducedMotion && status === "queue" && queuePosition != null && (
          <m.div
            key={`tick-${queuePosition}`}
            className="qs-tickflash"
            initial={{ opacity: 0.5 }}
            animate={{ opacity: 0 }}
            transition={{ duration: 0.7, ease: "easeOut" }}
          />
        )}
      </div>

      <header className="qs-topbar">
        <div className="qs-eyebrow">
          <Radio size={13} className="qs-eyebrow-icon" />
          <span>{t("streamLoading.labels.nowLoading")}</span>
        </div>
        <div className="qs-topbar-right">
          <div className="qs-elapsed">
            <span className="qs-elapsed-label">{t("streamLoading.telemetry.elapsed")}</span>
            <span className="qs-elapsed-value">{formatElapsed(elapsed)}</span>
          </div>
          <button
            type="button"
            className="qs-iconbtn"
            aria-label={t("streamLoading.actions.cancelLoading")}
            onClick={onCancel}
          >
            <X size={18} />
          </button>
        </div>
      </header>

      <main className="qs-main">
        {error ? (
          <div className="qs-error" role="alert">
            <div className="qs-error-icon">
              <AlertTriangle size={26} />
            </div>
            <div className="qs-error-eyebrow">{t("streamLoading.labels.launchError")}</div>
            <h1 className="qs-error-title">{error.title || t("streamLoading.status.gameLaunchFailed")}</h1>
            <p className="qs-error-desc">{error.description}</p>
            {error.code && <div className="qs-error-code">{error.code}</div>}
            <div className="qs-error-actions">
              {error.actionLabel && onErrorAction && (
                <button type="button" className="qs-btn qs-btn-primary" onClick={onErrorAction}>
                  <RotateCcw size={15} />
                  {error.actionLabel}
                </button>
              )}
              <button type="button" className="qs-btn qs-btn-ghost" onClick={onCancel}>
                {t("app.actions.close")}
              </button>
            </div>
          </div>
        ) : (
          <m.div
            className="qs-content"
            initial={{ opacity: 0, y: reducedMotion ? 0 : 18 }}
            animate={{ opacity: 1, y: 0 }}
            transition={entranceTransition}
          >
            <div className="qs-masthead">
              <div className="qs-kicker">{storeLabel && <span className="qs-store">{storeLabel}</span>}</div>
              <h1 className="qs-title" title={gameTitle}>
                {titleWords.map((word, i) => (
                  <m.span
                    key={`${word}-${i}`}
                    className="qs-title-word"
                    initial={{ opacity: 0, y: reducedMotion ? 0 : "0.55em" }}
                    animate={{ opacity: 1, y: 0 }}
                    transition={{ duration: 0.55, delay: 0.3 + i * 0.07, ease: [0.22, 1, 0.36, 1] }}
                  >
                    {word}
                    {i < titleWords.length - 1 ? "\u00A0" : ""}
                  </m.span>
                ))}
              </h1>
            </div>

            <div className="qs-stageline" role="group" aria-label={t("streamLoading.labels.launchProgress")}>
              {STAGE_KEYS.map((key, i) => (
                <div
                  key={key}
                  className={`qs-stage-item ${i === stageIndex ? "qs-stage-active" : ""} ${
                    i < stageIndex ? "qs-stage-done" : ""
                  }`}
                >
                  <span className="qs-stage-label">{t(`streamLoading.steps.${key}`)}</span>
                  {i === stageIndex && (
                    <m.span
                      className="qs-stage-underline"
                      layoutId="qs-stage-underline"
                      transition={
                        reducedMotion
                          ? { duration: 0.01 }
                          : { type: "spring", stiffness: 400, damping: 40 }
                      }
                    />
                  )}
                </div>
              ))}
            </div>

            <div className="qs-stage-track" aria-hidden="true">
              <m.span
                className="qs-stage-fill"
                initial={{ width: "0%" }}
                animate={{ width: `${stageProgress * 100}%` }}
                transition={{ duration: 0.7, ease: "easeInOut" }}
              />
            </div>

            <div className="qs-statusblock" role="status" aria-live="polite">
              <AnimatePresence mode="wait">
                <m.p
                  key={status}
                  className="qs-status-text"
                  initial={{ opacity: 0, y: reducedMotion ? 0 : 8 }}
                  animate={{ opacity: 1, y: 0 }}
                  exit={{ opacity: 0, y: reducedMotion ? 0 : -8 }}
                  transition={{ duration: reducedMotion ? 0.01 : 0.25 }}
                >
                  {status === "queue" && queuePosition != null ? (
                    <>
                      {t("streamLoading.hero.position")}{" "}
                      <span className="qs-dropzone">
                        <OdometerNumber value={queuePosition} reduced={reducedMotion} />
                        {!reducedMotion && (
                          <AnimatePresence>
                            {drops.map((drop) => (
                              <m.span
                                key={drop.id}
                                className="qs-drop"
                                initial={{ opacity: 0, y: 8 }}
                                animate={{ opacity: 1, y: -24 }}
                                exit={{ opacity: 0 }}
                                transition={{ duration: 0.9, ease: "easeOut" }}
                              >
                                -{drop.diff}
                              </m.span>
                            ))}
                          </AnimatePresence>
                        )}
                      </span>
                    </>
                  ) : (
                    statusText
                  )}
                </m.p>
              </AnimatePresence>
              <p className="qs-cozy-text">{cozyText}</p>

              <div className="qs-meta-row">
                {status === "queue" && (
                  <span className="qs-meta-item qs-meta-est">
                    {t("streamLoading.hero.estWait")}: {estimatedWait || t("streamLoading.telemetry.calculating")}
                  </span>
                )}
                {detailText && <span className="qs-meta-item qs-meta-detail">{detailText}</span>}
              </div>

              {diagnosticLine && <p className="qs-diagnostic">{diagnosticLine}</p>}
            </div>

            {(adsRequired || queuePaused || showAdPlayer) && (
              <div className="qs-adzone">
                <div className="qs-adzone-label">{t("streamLoading.labels.adQueue")}</div>

                {queuePaused && (
                  <div className="qs-ad-paused">
                    <span className="qs-rule" aria-hidden="true" />
                    <div>
                      <p className="qs-ad-paused-title">{t("streamLoading.status.queuePaused")}</p>
                      <p className="qs-ad-paused-sub">{t("streamLoading.ads.resumeToStayInQueue")}</p>
                    </div>
                  </div>
                )}

                {!queuePaused && adsRequired && (
                  <p className="qs-ad-message">
                    {adMessage || t("streamLoading.ads.playbackRequired")}
                    {adCount > 0 ? ` ${t("streamLoading.ads.availableForProgression", { count: adCount })}` : ""}
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
          </m.div>
        )}
      </main>

      {!error && (
        <footer className="qs-footer">
          <div className="qs-footer-session">
            <span className="qs-eq" aria-hidden="true">
              <span /><span /><span /><span /><span />
            </span>
            <span>{t("streamLoading.telemetry.session")} · {formatElapsed(elapsed)} · {localTime}</span>
          </div>
          <button type="button" className="qs-btn qs-btn-ghost qs-btn-cancel" onClick={onCancel}>
            {t("app.actions.cancel")}
          </button>
        </footer>
      )}
    </div>
  );
}
