import { Check, Monitor, X } from "lucide-react";
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
import { getStoreDisplayName } from "./GameCard";
import { QueueAdPreview, type QueueAdPlaybackEvent, type QueueAdPreviewHandle } from "./QueueAdPreview";
import { LazyShaderAtmosphere } from "./LazyShaderAtmosphere";
import { useTranslation } from "../i18n";

type TranslateFunction = typeof import("../i18n").t;

const launchStageIds = ["queue", "setup", "connecting", "ready"] as const;
type LaunchStageId = (typeof launchStageIds)[number];

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

const stageLabelKey: Record<LaunchStageId, string> = {
  queue: "streamLoading.steps.queue",
  setup: "streamLoading.steps.setup",
  connecting: "streamLoading.steps.connect",
  ready: "streamLoading.steps.ready",
};

function safeStageLabel(t: TranslateFunction, id: LaunchStageId): string {
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

function getQueueDetail(t: TranslateFunction, queuePosition: number | undefined): string {
  if (queuePosition) {
    return t("streamLoading.detail.queueMoving", { position: queuePosition });
  }
  return t("streamLoading.cozy.queue");
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

interface MetaSegment {
  key: string;
  label: string;
  value: string;
  accent?: boolean;
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
  const adSummary = getAdSummary(t, adState);
  const cachedAdMediaUrl = activeAdMediaUrl ?? getPreferredSessionAdMediaUrl(activeAd);
  const activeStage = getActiveStage(status);
  const isQueue = status === "queue";
  const isPaused = isSessionQueuePaused(adState);
  const hasAd = Boolean(activeAd && cachedAdMediaUrl);
  const showQueueNumber = !hasError && isQueue && typeof queuePosition === "number";
  const currentStageId = launchStageIds[Math.min(activeStage, 3)];
  const showEq = !hasError && (status === "starting" || status === "connecting");

  // Overall progress across the four launch stages (approximate, keeps things
  // moving even when the queue position is unknown).
  const stageProgress = hasError ? 0 : Math.min(1, (activeStage + 0.5) / launchStageIds.length);

  const metaSegments: MetaSegment[] = [];
  if (showQueueNumber) {
    metaSegments.push({
      key: "pos",
      label: t("streamLoading.hero.position"),
      value: `#${queuePosition}`,
      accent: true,
    });
  }
  metaSegments.push({
    key: "elapsed",
    label: t("streamLoading.telemetry.elapsed"),
    value: formatWaitTime(elapsedSeconds),
  });
  metaSegments.push({
    key: "est",
    label: t("streamLoading.hero.estWait"),
    value: estimatedWait ?? (isQueue ? t("streamLoading.telemetry.calculating") : "—"),
  });

  const posterBadge = hasError
    ? t("streamLoading.labels.launchError")
    : showQueueNumber
      ? `#${queuePosition} · ${t("streamLoading.hero.inQueue")}`
      : safeStageLabel(t, currentStageId);

  const eyebrowParts = [
    hasError ? t("streamLoading.labels.launchError") : t("streamLoading.labels.nowLoading"),
    platformName,
  ].filter(Boolean);

  useEffect(() => {
    if (hasError) return undefined;
    const timer = window.setInterval(() => {
      setElapsedSeconds(Math.floor((Date.now() - startedAt) / 1000));
    }, 1000);
    return () => window.clearInterval(timer);
  }, [hasError, startedAt]);

  return (
    <div className={`gload${hasError ? " gload--error" : ""}`}>
      {/* Full-bleed cover backdrop */}
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
          <span className="gload-brand-text">OpenNOW</span>
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

      {/* Main stage — poster + editorial status column */}
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
            <div className={`gload-poster-badge${hasError ? " gload-poster-badge--error" : ""}`}>
              {!hasError && <span className="gload-poster-badge-dot" aria-hidden="true" />}
              <span>{posterBadge}</span>
            </div>
          </div>
        </m.div>

        <m.div
          className="gload-info"
          initial={{ opacity: 0, x: 26 }}
          animate={{ opacity: 1, x: 0 }}
          transition={{ duration: 0.55, ease: [0.16, 1, 0.3, 1], delay: 0.08 }}
        >
          <span className={`gload-eyebrow${hasError ? " gload-eyebrow--error" : ""}`}>
            {!hasError && <span className="gload-eyebrow-dot" aria-hidden="true" />}
            {eyebrowParts.join("  ·  ")}
          </span>
          <h1 className="gload-title" title={gameTitle}>{gameTitle}</h1>

          {showQueueNumber ? (
            <div className="gload-queue" role="status" aria-live="polite">
              <span className="gload-queue-label">{t("streamLoading.hero.placeInQueue")}</span>
              <div className="gload-queue-numwrap">
                <AnimatePresence mode="popLayout" initial={false}>
                  <m.span
                    key={queuePosition}
                    className="gload-queue-num"
                    initial={{ y: 60, opacity: 0, scale: 0.86, filter: "blur(10px)" }}
                    animate={{ y: 0, opacity: 1, scale: 1, filter: "blur(0px)" }}
                    exit={{ y: -60, opacity: 0, scale: 0.92, filter: "blur(10px)" }}
                    transition={{ type: "spring", stiffness: 300, damping: 30 }}
                  >
                    #{queuePosition}
                  </m.span>
                </AnimatePresence>
              </div>
              <p className="gload-queue-detail">{getQueueDetail(t, queuePosition)}</p>
            </div>
          ) : !hasError ? (
            <div className="gload-phase" role="status" aria-live="polite">
              <p className="gload-phase-title">{statusMessage}</p>
              {showEq && (
                <div className="gload-eq" aria-hidden="true">
                  <span /><span /><span /><span /><span /><span /><span />
                </div>
              )}
              <p className="gload-phase-detail">{getPhaseDetail(t, status)}</p>
            </div>
          ) : (
            <div className="gload-fail" role="alert">
              <p className="gload-phase-title">{error?.title ?? statusMessage}</p>
              {error && <p className="gload-error-desc">{error.description}</p>}
              {error?.code && <span className="gload-error-code">{error.code}</span>}
            </div>
          )}

          {/* Meta strip */}
          {!hasError && (
            <div className="gload-meta">
              {metaSegments.map((segment) => (
                <span
                  key={segment.key}
                  className={`gload-meta-item${segment.accent ? " gload-meta-item--accent" : ""}`}
                >
                  <span className="gload-meta-label">{segment.label}</span>
                  <span className="gload-meta-value">{segment.value}</span>
                </span>
              ))}
            </div>
          )}
          {!hasError && diagnosticLine && (
            <p className="gload-diagnostic">{diagnosticLine}</p>
          )}

          {/* Progress line */}
          {!hasError && (
            <div className="gload-progress" aria-hidden="true">
              <div className="gload-progress-head">
                <span>{safeStageLabel(t, currentStageId)}</span>
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

          {/* Stage breadcrumb */}
          {!hasError && (
            <div className="gload-crumbs" aria-label={t("streamLoading.labels.launchProgress")}>
              {launchStageIds.map((stageId, index) => {
                const state = index < activeStage ? "done" : index === activeStage ? "active" : "todo";
                return (
                  <span className={`gload-crumb gload-crumb--${state}`} key={stageId}>
                    {state === "done" && <Check size={12} />}
                    {state === "active" && <span className="gload-crumb-dot" aria-hidden="true" />}
                    <span>{safeStageLabel(t, stageId)}</span>
                  </span>
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
