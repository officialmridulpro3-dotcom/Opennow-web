import { Cpu, Gauge, Monitor, Radio, Wifi, X, XCircle, Check } from "lucide-react";
import { useEffect, useState } from "react";
import type { JSX, Ref } from "react";
import { AnimatePresence, m } from "motion/react";
import {
  getPreferredSessionAdMediaUrl,
  getSessionAdItems,
  getSessionAdMessage,
  isSessionAdsRequired,
  isSessionQueuePaused,
} from "@shared/gfn";
import type { SessionAdInfo, SessionAdState } from "@shared/gfn";
import { getStoreDisplayName, getStoreIconComponent } from "./GameCard";
import { QueueAdPreview, type QueueAdPlaybackEvent, type QueueAdPreviewHandle } from "./QueueAdPreview";
import { LazyShaderAtmosphere } from "./LazyShaderAtmosphere";
import { useTranslation } from "../i18n";

type TranslateFunction = typeof import("../i18n").t;

const launchStages = [
  { id: "queue", icon: Radio },
  { id: "setup", icon: Cpu },
  { id: "connecting", icon: Wifi },
  { id: "ready", icon: Monitor },
] as const;

export interface StreamLoadingProps {
  gameTitle: string;
  gameCover?: string;
  platformStore?: string;
  status: "queue" | "setup" | "starting" | "connecting";
  queuePosition?: number;
  /** Small live diagnostic line under the status (poll attempt, seat status). */
  diagnosticLine?: string | null;
  estimatedWait?: string;
  adState?: SessionAdState;
  activeAd?: SessionAdInfo;
  activeAdMediaUrl?: string;
  error?: {
    title: string;
    description: string;
    code?: string;
    actionLabel?: string;
  };
  onAdPlaybackEvent?: (event: QueueAdPlaybackEvent, adId: string) => void;
  adPreviewRef?: Ref<QueueAdPreviewHandle>;
  onErrorAction?: () => void;
  onCancel: () => void;
}

const stageLabelKey: Record<(typeof launchStages)[number]["id"], string> = {
  queue: "streamLoading.steps.queue",
  setup: "streamLoading.steps.setup",
  connecting: "streamLoading.steps.connect",
  ready: "streamLoading.steps.ready",
};

function safeStageLabel(t: TranslateFunction, id: (typeof launchStages)[number]["id"]): string {
  const fallback: Record<string, string> = {
    queue: "Queue",
    setup: "Setting up",
    connecting: "Connecting",
    ready: "Ready",
  };
  const key = stageLabelKey[id];
  const translated = t(key);
  return translated === key ? fallback[id] : translated;
}

function getStatusMessage(
  t: TranslateFunction,
  status: StreamLoadingProps["status"],
  queuePosition?: number,
  adState?: SessionAdState,
  isError = false,
): string {
  if (isError) return t("streamLoading.status.gameLaunchFailed");
  if (isSessionQueuePaused(adState)) return t("streamLoading.status.queuePaused");

  switch (status) {
    case "queue":
      return queuePosition
        ? t("streamLoading.status.positionInQueue", { position: queuePosition })
        : t("streamLoading.status.waitingInQueue");
    case "setup":
      return t("streamLoading.status.settingUpRig");
    case "starting":
      return t("streamLoading.status.startingStream");
    case "connecting":
      return t("streamLoading.status.connectingToServer");
  }
}

function getPhaseDetail(t: TranslateFunction, status: StreamLoadingProps["status"]): string {
  switch (status) {
    case "queue":
      return t("streamLoading.cozy.queue");
    case "setup":
      return t("streamLoading.cozy.setup");
    case "starting":
      return t("streamLoading.cozy.starting");
    case "connecting":
      return t("streamLoading.cozy.connecting");
  }
}

function getActiveStage(status: StreamLoadingProps["status"]): number {
  if (status === "queue") return 0;
  if (status === "setup") return 1;
  return 2;
}

function formatWaitTime(totalSeconds: number): string {
  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;
  return `${minutes.toString().padStart(2, "0")}:${seconds.toString().padStart(2, "0")}`;
}

function getAdSummary(t: TranslateFunction, adState?: SessionAdState): string | null {
  if (!isSessionAdsRequired(adState)) return null;
  const message = getSessionAdMessage(adState);
  if (message) return message;
  if (isSessionQueuePaused(adState)) return t("streamLoading.ads.resumeToStayInQueue");
  const ads = getSessionAdItems(adState);
  return ads.length > 0
    ? t("streamLoading.ads.availableForProgression", { count: ads.length })
    : t("streamLoading.ads.playbackRequired");
}

export function StreamLoading({
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
}: StreamLoadingProps): JSX.Element {
  const { t } = useTranslation();
  const [startedAt] = useState(() => Date.now());
  const [elapsedSeconds, setElapsedSeconds] = useState(0);
  const hasError = Boolean(error);
  const statusMessage = getStatusMessage(t, status, queuePosition, adState, hasError);
  const platformName = platformStore ? getStoreDisplayName(platformStore) : "";
  const PlatformIcon = platformStore ? getStoreIconComponent(platformStore) : null;
  const adSummary = getAdSummary(t, adState);
  const cachedAdMediaUrl = activeAdMediaUrl ?? getPreferredSessionAdMediaUrl(activeAd);
  const activeStage = getActiveStage(status);
  const isQueue = status === "queue";
  const isPaused = isSessionQueuePaused(adState);
  const hasAd = Boolean(activeAd && cachedAdMediaUrl);

  // Overall progress across the four launch stages (approximate, keeps the ring
  // moving even when the queue position is unknown).
  const stageProgress = hasError ? 0 : Math.min(1, (activeStage + 0.5) / launchStages.length);
  const ringCircumference = 2 * Math.PI * 52;

  useEffect(() => {
    if (hasError) return undefined;
    const timer = window.setInterval(() => {
      setElapsedSeconds(Math.floor((Date.now() - startedAt) / 1000));
    }, 1000);
    return () => window.clearInterval(timer);
  }, [hasError, startedAt]);

  return (
    <div className={`gload${hasError ? " gload--error" : ""}`}>
      {/* Full-bleed blurred cover backdrop */}
      {gameCover ? (
        <div className="gload-bg" style={{ backgroundImage: `url(${gameCover})` }} />
      ) : (
        <div className="gload-bg gload-bg--empty" />
      )}
      {!hasError && (
        <LazyShaderAtmosphere variant={isQueue ? "queue" : "connecting"} className="gload-shader" />
      )}
      <div className="gload-vignette" />
      <div className="gload-grain" aria-hidden="true" />

      {/* Top bar */}
      <div className="gload-topbar">
        <div className="gload-brand">
          <span className={`gload-brand-dot${hasError ? " gload-brand-dot--error" : ""}`} />
          <span className="gload-brand-text">
            {hasError ? t("streamLoading.labels.launchError") : t("streamLoading.labels.nowLoading")}
          </span>
        </div>
        <button
          type="button"
          className="gload-close"
          onClick={onCancel}
          aria-label={t("streamLoading.actions.cancelLoading")}
        >
          <X size={18} />
        </button>
      </div>

      {/* Main stage — big poster + info */}
      <div className="gload-stage">
        <m.div
          className="gload-poster-wrap"
          initial={{ opacity: 0, scale: 0.94, y: 16 }}
          animate={{ opacity: 1, scale: 1, y: 0 }}
          transition={{ duration: 0.6, ease: [0.16, 1, 0.3, 1] }}
        >
          <div className="gload-poster-halo" aria-hidden="true" />
          <div className="gload-poster">
            {gameCover ? (
              <img src={gameCover} alt="" className="gload-poster-img" />
            ) : (
              <div className="gload-poster-empty"><Monitor size={64} /></div>
            )}
            {!hasError && (
              <m.span
                className="gload-poster-sheen"
                aria-hidden="true"
                animate={{ x: ["-130%", "230%"] }}
                transition={{ duration: 3, repeat: Infinity, ease: "easeInOut", repeatDelay: 1.6 }}
              />
            )}
            <div className="gload-poster-reflection" aria-hidden="true" />
          </div>
        </m.div>

        <m.div
          className="gload-info"
          initial={{ opacity: 0, x: 26 }}
          animate={{ opacity: 1, x: 0 }}
          transition={{ duration: 0.55, ease: [0.16, 1, 0.3, 1], delay: 0.08 }}
        >
          <span className={`gload-eyebrow${hasError ? " gload-eyebrow--error" : ""}`}>
            {hasError ? t("streamLoading.labels.launchError") : t("streamLoading.labels.nowLoading")}
          </span>
          <h1 className="gload-title" title={gameTitle}>{gameTitle}</h1>

          <div className="gload-tags">
            {PlatformIcon && (
              <span className="gload-tag" title={platformName}>
                <span className="gload-tag-icon"><PlatformIcon /></span>
                <span>{platformName}</span>
              </span>
            )}
            <span className="gload-tag">
              <Gauge size={14} />
              <span>{formatWaitTime(elapsedSeconds)}</span>
            </span>
            {isQueue && queuePosition ? (
              <span className="gload-tag gload-tag--accent">
                <Radio size={14} />
                <span>{t("streamLoading.telemetry.queuePosition")} #{queuePosition}</span>
              </span>
            ) : null}
          </div>

          <div className={`gload-status${hasError ? " gload-status--error" : ""}`}>
            {hasError ? (
              <XCircle size={20} className="gload-status-icon" />
            ) : (
              <m.span
                className="gload-live-dot"
                aria-hidden="true"
                animate={{ opacity: [0.5, 1, 0.5], scale: [0.85, 1.15, 0.85] }}
                transition={{ duration: 1.6, repeat: Infinity, ease: "easeInOut" }}
              />
            )}
            <div className="gload-status-text">
              <p className="gload-message" role="status" aria-live="polite">{statusMessage}</p>
              {!hasError && <p className="gload-detail">{getPhaseDetail(t, status)}</p>}
              {!hasError && diagnosticLine && (
                <p className="gload-detail" style={{ opacity: 0.75 }}>{diagnosticLine}</p>
              )}
              {hasError && error && (
                <>
                  <p className="gload-error-desc">{error.description}</p>
                  {error.code && <span className="gload-error-code">{error.code}</span>}
                </>
              )}
            </div>
          </div>

          {/* Progress bar */}
          {!hasError && (
            <div className="gload-progress" aria-hidden="true">
              <div className="gload-progress-head">
                <span>{safeStageLabel(t, launchStages[Math.min(activeStage, 3)].id)}</span>
                <span>{isQueue && queuePosition ? `#${queuePosition}` : `${Math.round(stageProgress * 100)}%`}</span>
              </div>
              <div className="gload-progress-track">
                <m.span
                  className="gload-progress-fill"
                  initial={{ width: "0%" }}
                  animate={{ width: `${stageProgress * 100}%` }}
                  transition={{ duration: 0.8, ease: "easeInOut" }}
                />
              </div>
            </div>
          )}

          {/* Stage stepper */}
          {!hasError && (
            <div className="gload-steps" aria-label={t("streamLoading.labels.launchProgress")}>
              {launchStages.map((stage, index) => {
                const StageIcon = stage.icon;
                const state = index < activeStage ? "completed" : index === activeStage ? "active" : "pending";
                return (
                  <div className={`gload-step gload-step--${state}`} key={stage.id}>
                    <m.span
                      className="gload-step-icon"
                      animate={state === "active" ? { scale: [1, 1.12, 1] } : { scale: 1 }}
                      transition={state === "active"
                        ? { duration: 1.6, repeat: Infinity, ease: "easeInOut" }
                        : { duration: 0.2 }}
                    >
                      {state === "completed" ? <Check size={16} /> : <StageIcon size={16} />}
                    </m.span>
                    <span className="gload-step-name">{safeStageLabel(t, stage.id)}</span>
                    {index < launchStages.length - 1 && (
                      <span className={`gload-step-line${index < activeStage ? " gload-step-line--done" : ""}`} />
                    )}
                  </div>
                );
              })}
            </div>
          )}

          {/* Ad preview */}
          <AnimatePresence>
            {!hasError && hasAd && (
              <m.div
                className={`gload-ad${isPaused ? " gload-ad--paused" : ""}`}
                initial={{ opacity: 0, height: 0 }}
                animate={{ opacity: 1, height: "auto" }}
                exit={{ opacity: 0, height: 0 }}
                transition={{ duration: 0.3 }}
              >
                <div className="gload-ad-copy">
                  <span className="gload-ad-chip">{t("streamLoading.labels.adQueue")}</span>
                  {adSummary && <p className="gload-ad-message">{adSummary}</p>}
                </div>
                <div className="gload-ad-media">
                  <QueueAdPreview
                    ref={adPreviewRef}
                    mediaUrl={cachedAdMediaUrl ?? ""}
                    title={activeAd!.title}
                    onPlaybackEvent={(event) => onAdPlaybackEvent?.(event, activeAd!.adId)}
                  />
                </div>
              </m.div>
            )}
          </AnimatePresence>

          {/* Actions */}
          <div className="gload-actions">
            {hasError && error?.actionLabel && onErrorAction && (
              <button type="button" className="gload-btn gload-btn--primary" onClick={onErrorAction}>
                <span>{error.actionLabel}</span>
              </button>
            )}
            <button
              type="button"
              className="gload-btn gload-btn--ghost"
              onClick={onCancel}
              aria-label={t("streamLoading.actions.cancelLoading")}
            >
              <X size={15} />
              <span>{hasError ? t("app.actions.close") : t("app.actions.cancel")}</span>
            </button>
          </div>
        </m.div>
      </div>
    </div>
  );
}
