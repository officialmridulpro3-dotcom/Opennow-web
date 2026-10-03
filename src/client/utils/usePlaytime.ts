import { useCallback, useEffect, useMemo, useRef, useState } from "react";

import type { GameInfo } from "@shared/gfn";
import type { LegacyPlaytimeRecord, PlaytimeSessionPayload, PlaytimeSummary } from "@shared/playtime";
import { fetchPlaytimeSummary, importLegacyPlaytime, recordPlaytimeSession, resetPlaytimeLedger } from "../api";

/**
 * Playtime recorder.
 *
 * Counted time is *streamed* time only: the clock starts on the first decoded
 * frame and stops when the session ends. Waiting in queue and the post-queue
 * connection handshake are recorded separately, so a 20-minute queue never
 * shows up as 20 minutes of play.
 *
 * The ledger itself lives on the backend (app-data directory), which survives
 * WebView profile resets and reinstalls; localStorage only holds a cache for an
 * instant first paint and the pre-ledger counter that gets migrated once.
 */

const LEGACY_STORAGE_KEY = "opennow:playtime";
const SUMMARY_CACHE_KEY = "opennow:playtime:summary";
const HEARTBEAT_INTERVAL_MS = 60_000;
const MIN_RECORDED_SECONDS = 1;

export interface PlaytimeRecord {
  totalSeconds: number;
  lastPlayedAt: string | null;
  sessionCount: number;
}

/** Legacy-shaped per-game view, still consumed by the home and library shelves. */
export type PlaytimeStore = Record<string, PlaytimeRecord>;

/** Engine/region context recorded alongside a session. */
export interface PlaybackContext {
  region?: string | null;
  clientMode?: string | null;
}

/** Wait window opened when Play is pressed, before any frame arrives. */
interface LaunchContext {
  gameId: string;
  title: string;
  store: string | null;
  imageUrl: string | null;
  launchStartedAtMs: number;
  queueEndedAtMs: number | null;
  region: string | null;
  clientMode: string | null;
}

interface ActivePlayback {
  playbackId: string;
  gameId: string;
  title: string;
  store: string | null;
  imageUrl: string | null;
  launchStartedAtMs: number;
  queueEndedAtMs: number | null;
  streamStartedAtMs: number;
  region: string | null;
  clientMode: string | null;
}

function readSummaryCache(): PlaytimeSummary | null {
  try {
    const raw = localStorage.getItem(SUMMARY_CACHE_KEY);
    if (!raw) return null;
    const parsed = JSON.parse(raw) as PlaytimeSummary;
    return parsed && parsed.version === 1 && Array.isArray(parsed.games) ? parsed : null;
  } catch {
    return null;
  }
}

function writeSummaryCache(summary: PlaytimeSummary): void {
  try {
    localStorage.setItem(SUMMARY_CACHE_KEY, JSON.stringify(summary));
  } catch {
    // Cache is best-effort; the server stays the source of truth.
  }
}

function readLegacyRecords(): LegacyPlaytimeRecord[] {
  try {
    const raw = localStorage.getItem(LEGACY_STORAGE_KEY);
    if (!raw) return [];
    const parsed = JSON.parse(raw) as Record<string, PlaytimeRecord>;
    if (!parsed || typeof parsed !== "object") return [];
    return Object.entries(parsed)
      .filter(([gameId, value]) => gameId.length > 0 && value && Number(value.totalSeconds) > 0)
      .map(([gameId, value]) => ({
        gameId,
        totalSeconds: Number(value.totalSeconds) || 0,
        sessionCount: Number(value.sessionCount) || 0,
        lastPlayedAt: value.lastPlayedAt ?? null,
      }));
  } catch {
    return [];
  }
}

function clearLegacyRecords(): void {
  try {
    localStorage.removeItem(LEGACY_STORAGE_KEY);
  } catch {
    // Nothing else to do.
  }
}

function newPlaybackId(): string {
  try {
    if (typeof crypto !== "undefined" && "randomUUID" in crypto) return crypto.randomUUID();
  } catch {
    // Fall through.
  }
  return `play-${Date.now()}-${Math.random().toString(36).slice(2, 12)}`;
}

export function formatPlaytime(totalSeconds: number): string {
  if (totalSeconds < 60) {
    return totalSeconds <= 0 ? "Never played" : "< 1 min";
  }
  const h = Math.floor(totalSeconds / 3600);
  const m = Math.floor((totalSeconds % 3600) / 60);
  if (h === 0) return `${m} m`;
  if (m === 0) return `${h} h`;
  return `${h} h ${m} m`;
}

export function formatRemainingPlaytimeFromSubscription(
  subscription: { isUnlimited: boolean; remainingHours: number } | null,
  consumedHours = 0,
): string {
  if (!subscription) {
    return "--";
  }
  if (subscription.isUnlimited) {
    return "Unlimited";
  }

  const baseHours = Number.isFinite(subscription.remainingHours) ? subscription.remainingHours : 0;
  const safeHours = Math.max(0, baseHours - Math.max(0, consumedHours));
  const totalMinutes = Math.round(safeHours * 60);
  const hours = Math.floor(totalMinutes / 60);
  const minutes = totalMinutes % 60;

  if (hours > 0) {
    return `${hours}h ${minutes.toString().padStart(2, "0")}m`;
  }
  return `${minutes}m`;
}

export interface UsePlaytimeReturn {
  /** Legacy-shaped map for the home/library shelves and sort orders. */
  playtime: PlaytimeStore;
  /** Full server ledger: totals, ranked games, recent sessions, histograms. */
  summary: PlaytimeSummary | null;
  /** True while a streamed session is being counted. */
  isRecording: boolean;
  /** First-frame timestamp of the live session; pair with useElapsedSeconds. */
  streamStartedAtMs: number | null;
  refreshPlaytime: () => Promise<void>;
  /** Play was pressed: opens the wait window (queue + setup), no playtime yet. */
  markLaunchStart: (game: GameInfo, context?: PlaybackContext) => void;
  /** The queue phase ended; the remaining wait counts as setup. */
  markQueueEnd: () => void;
  /** First decoded frame: the playtime clock starts here. */
  markStreamStart: (game?: GameInfo | null, context?: PlaybackContext) => void;
  /** Session over: flush the record to the ledger. */
  endSession: (gameId?: string) => void;
  /** Wipes the ledger (settings / Playtime page). */
  resetPlaytime: () => Promise<void>;
}

export function usePlaytime(): UsePlaytimeReturn {
  const [summary, setSummary] = useState<PlaytimeSummary | null>(readSummaryCache);
  const [streamStartedAtMs, setStreamStartedAtMs] = useState<number | null>(null);
  const activeRef = useRef<ActivePlayback | null>(null);
  const launchContextRef = useRef<LaunchContext | null>(null);
  const heartbeatRef = useRef<number | null>(null);
  const migratedRef = useRef(false);

  const applySummary = useCallback((next: PlaytimeSummary): void => {
    setSummary(next);
    writeSummaryCache(next);
  }, []);

  const upload = useCallback((payload: PlaytimeSessionPayload, keepalive = false): void => {
    recordPlaytimeSession(payload, { keepalive }).then(applySummary).catch(() => {
      // Offline or mid-restart: the next heartbeat carries the full running
      // total, so at most the tail of the current session is lost.
    });
  }, [applySummary]);

  const buildPayload = useCallback((playback: ActivePlayback, endedAtMs: number, partial: boolean): PlaytimeSessionPayload => {
    const streamedSeconds = Math.max(0, Math.floor((endedAtMs - playback.streamStartedAtMs) / 1000));
    const queueEnd = playback.queueEndedAtMs ?? playback.streamStartedAtMs;
    const queueSeconds = Math.max(0, Math.floor((queueEnd - playback.launchStartedAtMs) / 1000));
    const setupSeconds = Math.max(0, Math.floor((playback.streamStartedAtMs - queueEnd) / 1000));
    return {
      playbackId: playback.playbackId,
      gameId: playback.gameId,
      title: playback.title,
      store: playback.store,
      imageUrl: playback.imageUrl,
      startedAt: new Date(playback.streamStartedAtMs).toISOString(),
      endedAt: new Date(endedAtMs).toISOString(),
      seconds: streamedSeconds,
      queueSeconds,
      setupSeconds,
      region: playback.region,
      clientMode: playback.clientMode,
      partial,
    };
  }, []);

  const stopHeartbeat = useCallback((): void => {
    if (heartbeatRef.current !== null) {
      window.clearInterval(heartbeatRef.current);
      heartbeatRef.current = null;
    }
  }, []);

  const markLaunchStart = useCallback((game: GameInfo, context: PlaybackContext = {}): void => {
    // Opens the wait window (queue + setup). No playtime is counted until the
    // first frame arrives, and a launch that never streams records nothing.
    activeRef.current = null;
    launchContextRef.current = {
      gameId: game.id,
      title: game.title || game.id,
      store: game.availableStores?.[0] ?? null,
      imageUrl: game.heroImageUrl ?? game.imageUrl ?? null,
      launchStartedAtMs: Date.now(),
      queueEndedAtMs: null,
      region: context.region ?? null,
      clientMode: context.clientMode ?? null,
    };
  }, []);

  const markQueueEnd = useCallback((): void => {
    const context = launchContextRef.current;
    if (!context || context.queueEndedAtMs !== null) return;
    context.queueEndedAtMs = Date.now();
  }, []);

  const markStreamStart = useCallback((game?: GameInfo | null, playbackContext: PlaybackContext = {}): void => {
    if (activeRef.current) return;
    const context = launchContextRef.current;
    const now = Date.now();
    const gameId = game?.id ?? context?.gameId;
    if (!gameId) return;
    activeRef.current = {
      playbackId: newPlaybackId(),
      gameId,
      title: game?.title || context?.title || gameId,
      store: game?.availableStores?.[0] ?? context?.store ?? null,
      imageUrl: game?.heroImageUrl ?? game?.imageUrl ?? context?.imageUrl ?? null,
      launchStartedAtMs: context?.launchStartedAtMs ?? now,
      queueEndedAtMs: context?.queueEndedAtMs ?? null,
      streamStartedAtMs: now,
      region: playbackContext.region ?? context?.region ?? null,
      clientMode: playbackContext.clientMode ?? context?.clientMode ?? null,
    };
    launchContextRef.current = null;
    setStreamStartedAtMs(now);
    stopHeartbeat();
    // Heartbeat: a crash or a killed window loses at most one interval.
    heartbeatRef.current = window.setInterval(() => {
      const playback = activeRef.current;
      if (!playback) return;
      const payload = buildPayload(playback, Date.now(), true);
      if (payload.seconds < MIN_RECORDED_SECONDS) return;
      upload(payload);
    }, HEARTBEAT_INTERVAL_MS);
  }, [buildPayload, stopHeartbeat, upload]);

  const endSession = useCallback((_gameId?: string): void => {
    const playback = activeRef.current;
    activeRef.current = null;
    launchContextRef.current = null;
    stopHeartbeat();
    setStreamStartedAtMs(null);
    if (!playback) return;
    const payload = buildPayload(playback, Date.now(), false);
    if (payload.seconds < MIN_RECORDED_SECONDS) return;
    upload(payload);
  }, [buildPayload, stopHeartbeat, upload]);

  const refreshPlaytime = useCallback(async (): Promise<void> => {
    try {
      applySummary(await fetchPlaytimeSummary());
    } catch {
      // Keep the cached summary when the backend is unreachable.
    }
  }, [applySummary]);

  const resetPlaytime = useCallback(async (): Promise<void> => {
    try {
      applySummary(await resetPlaytimeLedger());
    } catch {
      // The page re-reads the summary on its next mount.
    }
    clearLegacyRecords();
  }, [applySummary]);

  // Boot: cache -> server -> one-shot migration of the pre-ledger counter.
  useEffect(() => {
    let cancelled = false;
    const boot = async (): Promise<void> => {
      try {
        const fresh = await fetchPlaytimeSummary();
        if (cancelled) return;
        applySummary(fresh);
        if (fresh.legacyImported || migratedRef.current) return;
        migratedRef.current = true;
        const legacy = readLegacyRecords();
        if (legacy.length === 0) return;
        applySummary(await importLegacyPlaytime(legacy));
        if (cancelled) return;
        clearLegacyRecords();
      } catch {
        // Backend unreachable at startup: the cached summary stays on screen.
      }
    };
    void boot();
    return () => {
      cancelled = true;
    };
  }, [applySummary]);

  // Hiding or closing the window must not drop the tail of a session. These are
  // partial flushes: the playback stays open, so alt-tabbing mid-stream and
  // resuming still ends in one complete record (the ledger keeps the maximum).
  useEffect(() => {
    const flushPartial = (): void => {
      const playback = activeRef.current;
      if (!playback) return;
      const payload = buildPayload(playback, Date.now(), true);
      if (payload.seconds < MIN_RECORDED_SECONDS) return;
      upload(payload, true);
    };
    const onPageHide = (): void => flushPartial();
    const onVisibilityChange = (): void => {
      if (document.visibilityState === "hidden") flushPartial();
    };
    window.addEventListener("pagehide", onPageHide);
    document.addEventListener("visibilitychange", onVisibilityChange);
    return () => {
      window.removeEventListener("pagehide", onPageHide);
      document.removeEventListener("visibilitychange", onVisibilityChange);
    };
  }, [buildPayload, upload]);

  // Leaving the page (or a hot reload) stops the heartbeat; the session itself
  // is ended by the app's stop paths.
  useEffect(() => stopHeartbeat, [stopHeartbeat]);

  const playtime = useMemo<PlaytimeStore>(() => {
    const store: PlaytimeStore = {};
    for (const game of summary?.games ?? []) {
      store[game.gameId] = {
        totalSeconds: game.totalSeconds,
        lastPlayedAt: game.lastPlayedAt,
        sessionCount: game.sessionCount,
      };
    }
    return store;
  }, [summary]);

  return {
    playtime,
    summary,
    isRecording: streamStartedAtMs !== null,
    streamStartedAtMs,
    refreshPlaytime,
    markLaunchStart,
    markQueueEnd,
    markStreamStart,
    endSession,
    resetPlaytime,
  };
}
