/**
 * Playtime ledger types, shared by the browser recorder and the backend store.
 *
 * Semantics are deliberately narrow: `seconds` is *streamed* time only — from
 * the first decoded frame until the session ends. Waiting in queue and the
 * post-queue connection/setup handshake are counted separately, so a slow queue
 * never inflates somebody's playtime.
 */

/** One upload from the client: a finished session, or a heartbeat while it runs. */
export interface PlaytimeSessionPayload {
  /** Client-generated id; repeats of the same id update rather than duplicate. */
  playbackId: string;
  gameId: string;
  title?: string | null;
  store?: string | null;
  imageUrl?: string | null;
  /** ISO timestamp of the first decoded frame. */
  startedAt: string;
  /** ISO timestamp of the last heartbeat or of session end. */
  endedAt: string;
  /** Streamed seconds (first frame -> end). */
  seconds: number;
  /** Seconds spent waiting in the queue before playback. */
  queueSeconds?: number;
  /** Seconds spent connecting/loading after the queue, before the first frame. */
  setupSeconds?: number;
  region?: string | null;
  /** "webrtc" | "native" — the streaming engine used. */
  clientMode?: string | null;
  /** True for mid-session heartbeats, which a later upload may extend. */
  partial?: boolean;
}

export interface PlaytimeSessionRecord {
  playbackId: string;
  gameId: string;
  title: string;
  store: string | null;
  imageUrl: string | null;
  startedAt: string;
  endedAt: string;
  seconds: number;
  queueSeconds: number;
  setupSeconds: number;
  region: string | null;
  clientMode: string | null;
  /** True for records migrated from the pre-ledger localStorage counter. */
  legacy?: boolean;
}

export interface PlaytimeGameAggregate {
  gameId: string;
  title: string;
  store: string | null;
  imageUrl: string | null;
  totalSeconds: number;
  queueSeconds: number;
  setupSeconds: number;
  sessionCount: number;
  longestSessionSeconds: number;
  firstPlayedAt: string | null;
  lastPlayedAt: string | null;
}

export interface PlaytimeTotals {
  streamedSeconds: number;
  queueSeconds: number;
  setupSeconds: number;
  sessionCount: number;
  gamesPlayed: number;
  longestSessionSeconds: number;
  averageSessionSeconds: number;
  firstPlayedAt: string | null;
  lastPlayedAt: string | null;
}

export interface PlaytimeDailyEntry {
  /** Local calendar day, YYYY-MM-DD. */
  date: string;
  seconds: number;
  sessionCount: number;
}

export interface PlaytimeSummary {
  version: 1;
  updatedAt: string | null;
  /** Whether pre-ledger localStorage history has been folded in already. */
  legacyImported: boolean;
  totals: PlaytimeTotals;
  /** Per-game aggregates, ranked by streamed seconds (descending). */
  games: PlaytimeGameAggregate[];
  /** Newest finished sessions first. */
  recent: PlaytimeSessionRecord[];
  /** Streamed seconds bucketed by local hour of day (index 0 = 00:00). */
  hourlySeconds: number[];
  /** Streamed seconds bucketed by weekday (index 0 = Sunday). */
  weekdaySeconds: number[];
  /** Recent calendar days, oldest first. */
  daily: PlaytimeDailyEntry[];
}

/** Legacy `opennow:playtime` localStorage shape, accepted once by the importer. */
export interface LegacyPlaytimeRecord {
  gameId: string;
  title?: string | null;
  store?: string | null;
  imageUrl?: string | null;
  totalSeconds?: number;
  sessionCount?: number;
  lastPlayedAt?: string | null;
}

export interface PlaytimeImportPayload {
  records: LegacyPlaytimeRecord[];
}

export const PLAYTIME_MAX_SESSION_SECONDS = 24 * 60 * 60;
