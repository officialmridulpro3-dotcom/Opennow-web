import { appDataPath, readAppDataFile, removeAppDataFile, writeAppDataFile } from "./appData";
import type {
  LegacyPlaytimeRecord,
  PlaytimeDailyEntry,
  PlaytimeGameAggregate,
  PlaytimeSessionPayload,
  PlaytimeSessionRecord,
  PlaytimeSummary,
  PlaytimeTotals,
} from "@shared/playtime";
import { PLAYTIME_MAX_SESSION_SECONDS } from "@shared/playtime";

/**
 * On-disk playtime ledger.
 *
 * Lives in the per-installation app-data directory so statistics survive
 * WebView profile resets, app updates, and reinstalls of the client bundle —
 * none of which the previous localStorage-only counter survived. The browser
 * still keeps a cache for instant paint; this store is the source of truth.
 *
 * Uploads are idempotent per `playbackId`: a heartbeat and the final flush of
 * the same session merge into one record, so a reconnect or a duplicated
 * `pagehide` never double-counts.
 */

const PLAYTIME_FILE = "playtime.json";
const MAX_SESSION_RECORDS = 400;
const MAX_DAILY_ENTRIES = 120;
const MAX_RECENT_IN_SUMMARY = 24;
const EPOCH_ISO = new Date(0).toISOString();

interface StoredState {
  version: 1;
  legacyImported: boolean;
  updatedAt: string | null;
  games: Record<string, PlaytimeGameAggregate>;
  sessions: Record<string, PlaytimeSessionRecord>;
  hourlySeconds: number[];
  weekdaySeconds: number[];
  daily: Record<string, PlaytimeDailyEntry>;
}

let cache: StoredState | null = null;

function emptyState(): StoredState {
  return {
    version: 1,
    legacyImported: false,
    updatedAt: null,
    games: {},
    sessions: {},
    hourlySeconds: new Array(24).fill(0),
    weekdaySeconds: new Array(7).fill(0),
    daily: {},
  };
}

function clampSeconds(value: unknown): number {
  const parsed = typeof value === "number" ? value : Number(value);
  if (!Number.isFinite(parsed) || parsed <= 0) return 0;
  return Math.min(PLAYTIME_MAX_SESSION_SECONDS, Math.floor(parsed));
}

function clampCount(value: unknown, limit = 1_000_000): number {
  const parsed = typeof value === "number" ? value : Number(value);
  if (!Number.isFinite(parsed) || parsed <= 0) return 0;
  return Math.min(limit, Math.floor(parsed));
}

function normalizeIso(value: unknown): string | null {
  if (typeof value !== "string" || value.trim().length === 0) return null;
  const ms = Date.parse(value);
  return Number.isFinite(ms) ? new Date(ms).toISOString() : null;
}

function pickText(...candidates: Array<string | null | undefined>): string | null {
  for (const candidate of candidates) {
    if (typeof candidate === "string" && candidate.trim().length > 0) return candidate.trim().slice(0, 512);
  }
  return null;
}

function earliestIso(left: string | null, right: string | null): string | null {
  if (!left) return right;
  if (!right) return left;
  return left <= right ? left : right;
}

function latestIso(left: string | null, right: string | null): string | null {
  if (!left) return right;
  if (!right) return left;
  return left >= right ? left : right;
}

function localDayKey(date: Date): string {
  const year = date.getFullYear();
  const month = `${date.getMonth() + 1}`.padStart(2, "0");
  const day = `${date.getDate()}`.padStart(2, "0");
  return `${year}-${month}-${day}`;
}

function sanitizeGame(raw: unknown, gameId: string): PlaytimeGameAggregate | null {
  if (!raw || typeof raw !== "object") return null;
  const value = raw as Partial<PlaytimeGameAggregate>;
  return {
    gameId,
    title: pickText(value.title) ?? gameId,
    store: pickText(value.store),
    imageUrl: pickText(value.imageUrl),
    totalSeconds: clampSeconds(value.totalSeconds),
    queueSeconds: clampSeconds(value.queueSeconds),
    setupSeconds: clampSeconds(value.setupSeconds),
    sessionCount: clampCount(value.sessionCount),
    longestSessionSeconds: clampSeconds(value.longestSessionSeconds),
    firstPlayedAt: normalizeIso(value.firstPlayedAt),
    lastPlayedAt: normalizeIso(value.lastPlayedAt),
  };
}

function sanitizeSession(raw: unknown): PlaytimeSessionRecord | null {
  if (!raw || typeof raw !== "object") return null;
  const value = raw as Partial<PlaytimeSessionRecord>;
  const playbackId = pickText(value.playbackId);
  const gameId = pickText(value.gameId);
  if (!playbackId || !gameId) return null;
  return {
    playbackId,
    gameId,
    title: pickText(value.title) ?? gameId,
    store: pickText(value.store),
    imageUrl: pickText(value.imageUrl),
    startedAt: normalizeIso(value.startedAt) ?? EPOCH_ISO,
    endedAt: normalizeIso(value.endedAt) ?? EPOCH_ISO,
    seconds: clampSeconds(value.seconds),
    queueSeconds: clampSeconds(value.queueSeconds),
    setupSeconds: clampSeconds(value.setupSeconds),
    region: pickText(value.region),
    clientMode: pickText(value.clientMode),
    ...(value.legacy === true ? { legacy: true } : {}),
  };
}

function sanitizeState(raw: string): StoredState {
  const state = emptyState();
  try {
    const parsed = JSON.parse(raw) as Partial<StoredState>;
    if (!parsed || typeof parsed !== "object") return state;
    state.legacyImported = parsed.legacyImported === true;
    state.updatedAt = normalizeIso(parsed.updatedAt);
    if (parsed.games && typeof parsed.games === "object") {
      for (const [gameId, value] of Object.entries(parsed.games)) {
        const game = sanitizeGame(value, gameId);
        if (game) state.games[gameId] = game;
      }
    }
    if (parsed.sessions && typeof parsed.sessions === "object") {
      for (const value of Object.values(parsed.sessions)) {
        const session = sanitizeSession(value);
        if (session) state.sessions[session.playbackId] = session;
      }
    }
    if (Array.isArray(parsed.hourlySeconds)) {
      for (let hour = 0; hour < 24; hour += 1) {
        state.hourlySeconds[hour] = clampSeconds(parsed.hourlySeconds[hour]);
      }
    }
    if (Array.isArray(parsed.weekdaySeconds)) {
      for (let day = 0; day < 7; day += 1) {
        state.weekdaySeconds[day] = clampSeconds(parsed.weekdaySeconds[day]);
      }
    }
    if (parsed.daily && typeof parsed.daily === "object") {
      for (const [date, value] of Object.entries(parsed.daily)) {
        if (!/^\d{4}-\d{2}-\d{2}$/.test(date) || !value || typeof value !== "object") continue;
        state.daily[date] = {
          date,
          seconds: clampSeconds((value as PlaytimeDailyEntry).seconds),
          sessionCount: clampCount((value as PlaytimeDailyEntry).sessionCount),
        };
      }
    }
  } catch (error) {
    console.warn(`[Playtime] Ignoring unreadable ledger at ${appDataPath(PLAYTIME_FILE)}: ${String(error)}`);
    return emptyState();
  }
  return state;
}

function loadState(): StoredState {
  if (cache) return cache;
  const raw = readAppDataFile(PLAYTIME_FILE);
  cache = raw && raw.trim().length > 0 ? sanitizeState(raw) : emptyState();
  return cache;
}

function saveState(state: StoredState): void {
  writeAppDataFile(PLAYTIME_FILE, `${JSON.stringify(state, null, 1)}\n`);
}

/** Keeps the ledger bounded: oldest finished sessions go first, legacy rows last. */
function evictOldSessions(state: StoredState): void {
  const keys = Object.keys(state.sessions);
  if (keys.length <= MAX_SESSION_RECORDS) return;
  const evictable = keys
    .map((key) => state.sessions[key])
    .filter((session) => session && session.legacy !== true)
    .sort((a, b) => a.endedAt.localeCompare(b.endedAt));
  const overflow = keys.length - MAX_SESSION_RECORDS;
  for (const session of evictable.slice(0, overflow)) {
    delete state.sessions[session.playbackId];
  }
}

function evictOldDaily(state: StoredState): void {
  const keys = Object.keys(state.daily).sort();
  if (keys.length <= MAX_DAILY_ENTRIES) return;
  for (const key of keys.slice(0, keys.length - MAX_DAILY_ENTRIES)) {
    delete state.daily[key];
  }
}

function buildSummary(state: StoredState): PlaytimeSummary {
  const games = Object.values(state.games).sort(
    (a, b) => b.totalSeconds - a.totalSeconds || a.title.localeCompare(b.title),
  );
  const recent = Object.values(state.sessions)
    .filter((session) => session.legacy !== true)
    .sort((a, b) => b.endedAt.localeCompare(a.endedAt))
    .slice(0, MAX_RECENT_IN_SUMMARY);

  const totals: PlaytimeTotals = {
    streamedSeconds: 0,
    queueSeconds: 0,
    setupSeconds: 0,
    sessionCount: 0,
    gamesPlayed: 0,
    longestSessionSeconds: 0,
    averageSessionSeconds: 0,
    firstPlayedAt: null,
    lastPlayedAt: null,
  };
  for (const game of games) {
    totals.streamedSeconds += game.totalSeconds;
    totals.queueSeconds += game.queueSeconds;
    totals.setupSeconds += game.setupSeconds;
    totals.sessionCount += game.sessionCount;
    totals.longestSessionSeconds = Math.max(totals.longestSessionSeconds, game.longestSessionSeconds);
    totals.firstPlayedAt = earliestIso(totals.firstPlayedAt, game.firstPlayedAt);
    totals.lastPlayedAt = latestIso(totals.lastPlayedAt, game.lastPlayedAt);
    if (game.totalSeconds > 0 || game.sessionCount > 0) totals.gamesPlayed += 1;
  }
  totals.averageSessionSeconds = totals.sessionCount > 0
    ? Math.round(totals.streamedSeconds / totals.sessionCount)
    : 0;

  const daily = Object.values(state.daily)
    .sort((a, b) => a.date.localeCompare(b.date))
    .slice(-MAX_DAILY_ENTRIES);

  return {
    version: 1,
    updatedAt: state.updatedAt,
    legacyImported: state.legacyImported,
    totals,
    games,
    recent,
    hourlySeconds: [...state.hourlySeconds],
    weekdaySeconds: [...state.weekdaySeconds],
    daily,
  };
}

/** Records (or extends, for heartbeats) one streamed session. */
export function recordPlaytimeSession(payload: PlaytimeSessionPayload): PlaytimeSummary {
  const state = loadState();
  const gameId = pickText(payload?.gameId);
  if (!gameId) return buildSummary(state);

  const playbackId = pickText(payload?.playbackId)?.slice(0, 128)
    ?? `auto-${Date.now()}-${Math.random().toString(36).slice(2, 10)}`;
  const existing = state.sessions[playbackId];
  // Legacy rows are placeholders without real timestamps; never extend them.
  if (existing?.legacy) return buildSummary(state);

  const seconds = Math.max(clampSeconds(payload.seconds), existing?.seconds ?? 0);
  const queueSeconds = Math.max(clampSeconds(payload.queueSeconds), existing?.queueSeconds ?? 0);
  const setupSeconds = Math.max(clampSeconds(payload.setupSeconds), existing?.setupSeconds ?? 0);
  const deltaSeconds = seconds - (existing?.seconds ?? 0);
  const deltaQueue = queueSeconds - (existing?.queueSeconds ?? 0);
  const deltaSetup = setupSeconds - (existing?.setupSeconds ?? 0);
  const isNew = !existing;

  const record: PlaytimeSessionRecord = {
    playbackId,
    gameId,
    title: pickText(payload.title, existing?.title) ?? gameId,
    store: pickText(payload.store, existing?.store),
    imageUrl: pickText(payload.imageUrl, existing?.imageUrl),
    // The first write of a playbackId owns the start time; later uploads only
    // extend the end, so a heartbeat cannot shift history.
    startedAt: existing?.startedAt ?? normalizeIso(payload.startedAt) ?? new Date().toISOString(),
    endedAt: latestIso(existing?.endedAt, normalizeIso(payload.endedAt) ?? new Date().toISOString()) ?? new Date().toISOString(),
    seconds,
    queueSeconds,
    setupSeconds,
    region: pickText(payload.region, existing?.region),
    clientMode: pickText(payload.clientMode, existing?.clientMode),
  };
  state.sessions[playbackId] = record;

  const startedDate = new Date(record.startedAt);
  if (Number.isFinite(startedDate.getTime())) {
    const dayKey = localDayKey(startedDate);
    const day = state.daily[dayKey] ?? { date: dayKey, seconds: 0, sessionCount: 0 };
    day.seconds += deltaSeconds;
    if (isNew) day.sessionCount += 1;
    state.daily[dayKey] = day;
    if (deltaSeconds > 0) {
      state.hourlySeconds[startedDate.getHours()] += deltaSeconds;
      state.weekdaySeconds[startedDate.getDay()] += deltaSeconds;
    }
  }

  const game = state.games[gameId] ?? {
    gameId,
    title: record.title,
    store: record.store,
    imageUrl: record.imageUrl,
    totalSeconds: 0,
    queueSeconds: 0,
    setupSeconds: 0,
    sessionCount: 0,
    longestSessionSeconds: 0,
    firstPlayedAt: null,
    lastPlayedAt: null,
  };
  game.title = record.title || game.title;
  game.store = record.store ?? game.store;
  game.imageUrl = record.imageUrl ?? game.imageUrl;
  game.totalSeconds += deltaSeconds;
  game.queueSeconds += deltaQueue;
  game.setupSeconds += deltaSetup;
  if (isNew) game.sessionCount += 1;
  game.longestSessionSeconds = Math.max(game.longestSessionSeconds, seconds);
  game.firstPlayedAt = earliestIso(game.firstPlayedAt, record.startedAt);
  game.lastPlayedAt = latestIso(game.lastPlayedAt, record.endedAt);
  state.games[gameId] = game;

  state.updatedAt = new Date().toISOString();
  evictOldSessions(state);
  evictOldDaily(state);
  saveState(state);
  return buildSummary(state);
}

/**
 * Folds the pre-ledger localStorage counter into the store, once. The old
 * records carry no per-session timestamps, so they land as `legacy` rows: they
 * contribute totals and per-game history but stay out of the recent list and
 * out of the hour/weekday/day histograms.
 */
export function importLegacyPlaytime(records: LegacyPlaytimeRecord[]): PlaytimeSummary {
  const state = loadState();
  if (state.legacyImported) {
    saveState(state);
    return buildSummary(state);
  }

  const incoming = Array.isArray(records) ? records : [];
  for (const entry of incoming) {
    const gameId = pickText(entry?.gameId);
    if (!gameId) continue;
    const playbackId = `legacy:${gameId}`;
    if (state.sessions[playbackId]) continue;

    const seconds = clampSeconds(entry.totalSeconds);
    const sessionCount = clampCount(entry.sessionCount, 100_000);
    if (seconds <= 0 && sessionCount <= 0) continue;
    const lastPlayedAt = normalizeIso(entry.lastPlayedAt);

    state.sessions[playbackId] = {
      playbackId,
      gameId,
      title: pickText(entry.title) ?? gameId,
      store: pickText(entry.store),
      imageUrl: pickText(entry.imageUrl),
      startedAt: lastPlayedAt ?? EPOCH_ISO,
      endedAt: lastPlayedAt ?? EPOCH_ISO,
      seconds,
      queueSeconds: 0,
      setupSeconds: 0,
      region: null,
      clientMode: null,
      legacy: true,
    };

    const game = state.games[gameId] ?? {
      gameId,
      title: pickText(entry.title) ?? gameId,
      store: pickText(entry.store),
      imageUrl: pickText(entry.imageUrl),
      totalSeconds: 0,
      queueSeconds: 0,
      setupSeconds: 0,
      sessionCount: 0,
      longestSessionSeconds: 0,
      firstPlayedAt: null,
      lastPlayedAt: null,
    };
    game.title = pickText(entry.title) ?? game.title;
    game.store = pickText(entry.store) ?? game.store;
    game.imageUrl = pickText(entry.imageUrl) ?? game.imageUrl;
    game.totalSeconds += seconds;
    game.sessionCount = Math.max(game.sessionCount, sessionCount);
    game.firstPlayedAt = earliestIso(game.firstPlayedAt, lastPlayedAt);
    game.lastPlayedAt = latestIso(game.lastPlayedAt, lastPlayedAt);
    state.games[gameId] = game;
  }

  state.legacyImported = true;
  state.updatedAt = new Date().toISOString();
  evictOldSessions(state);
  saveState(state);
  return buildSummary(state);
}

export function getPlaytimeSummary(): PlaytimeSummary {
  return buildSummary(loadState());
}

/**
 * Wipes the ledger. `legacyImported` stays set so a stale browser cache cannot
 * resurrect the pre-ledger history immediately after a reset.
 */
export function resetPlaytime(): PlaytimeSummary {
  const state = emptyState();
  state.legacyImported = true;
  cache = state;
  removeAppDataFile(PLAYTIME_FILE);
  saveState(state);
  return buildSummary(state);
}

export const playtimeStoreInternals = {
  clearCache(): void {
    cache = null;
  },
  filePath(): string {
    return appDataPath(PLAYTIME_FILE);
  },
  readRaw(): StoredState {
    return loadState();
  },
};
