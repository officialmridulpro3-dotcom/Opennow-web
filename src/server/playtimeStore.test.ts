import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";

import type { PlaytimeSessionPayload } from "@shared/playtime";
import {
  getPlaytimeSummary,
  importLegacyPlaytime,
  playtimeStoreInternals,
  recordPlaytimeSession,
  resetPlaytime,
} from "./playtimeStore";

let dataDir = "";

beforeEach(() => {
  dataDir = mkdtempSync(join(tmpdir(), "opennow-playtime-"));
  process.env.OPENNOW_DATA_DIR = dataDir;
  playtimeStoreInternals.clearCache();
});

afterEach(() => {
  delete process.env.OPENNOW_DATA_DIR;
  playtimeStoreInternals.clearCache();
  rmSync(dataDir, { recursive: true, force: true });
});

function session(overrides: Partial<PlaytimeSessionPayload> = {}): PlaytimeSessionPayload {
  const startedAt = overrides.startedAt ?? new Date("2026-09-20T18:00:00.000Z").toISOString();
  return {
    playbackId: overrides.playbackId ?? `play-${Math.random().toString(36).slice(2, 10)}`,
    gameId: "1001",
    title: "Neon Drift",
    store: "EPIC",
    imageUrl: "https://images.invalid/neon-drift.jpg",
    startedAt,
    endedAt: new Date(Date.parse(startedAt) + 1_800_000).toISOString(),
    seconds: 1_800,
    queueSeconds: 240,
    setupSeconds: 35,
    region: "EU-West",
    clientMode: "webrtc",
    ...overrides,
  };
}

describe("playtime ledger", () => {
  it("records streamed time and ranks games by playtime", () => {
    recordPlaytimeSession(session({ gameId: "1001", title: "Neon Drift", seconds: 1_800 }));
    recordPlaytimeSession(session({ gameId: "2002", title: "Astro Hollow", seconds: 7_200 }));
    recordPlaytimeSession(session({ gameId: "1001", title: "Neon Drift", seconds: 900 }));

    const summary = getPlaytimeSummary();
    expect(summary.games.map((game) => game.gameId)).toEqual(["2002", "1001"]);
    expect(summary.games[0].totalSeconds).toBe(7_200);
    expect(summary.games[1]).toMatchObject({ totalSeconds: 2_700, sessionCount: 2, longestSessionSeconds: 1_800 });
    expect(summary.totals.streamedSeconds).toBe(9_900);
    expect(summary.totals.gamesPlayed).toBe(2);
    expect(summary.totals.sessionCount).toBe(3);
    expect(summary.totals.averageSessionSeconds).toBe(3_300);
  });

  it("merges heartbeats for one playbackId instead of counting them twice", () => {
    const playbackId = "play-heartbeat";
    const first = recordPlaytimeSession(session({ playbackId, seconds: 600, partial: true }));
    expect(first.totals.streamedSeconds).toBe(600);
    expect(first.totals.sessionCount).toBe(1);

    const second = recordPlaytimeSession(session({ playbackId, seconds: 1_500, partial: true }));
    expect(second.totals.streamedSeconds).toBe(1_500);
    expect(second.totals.sessionCount).toBe(1);

    const final = recordPlaytimeSession(session({ playbackId, seconds: 1_800 }));
    expect(final.totals.streamedSeconds).toBe(1_800);
    expect(final.totals.sessionCount).toBe(1);
    expect(final.recent).toHaveLength(1);
    expect(final.games[0].longestSessionSeconds).toBe(1_800);
  });

  it("ignores a replayed upload that reports less time than already stored", () => {
    const playbackId = "play-replay";
    recordPlaytimeSession(session({ playbackId, seconds: 1_800 }));
    const summary = recordPlaytimeSession(session({ playbackId, seconds: 600 }));

    expect(summary.totals.streamedSeconds).toBe(1_800);
    expect(summary.totals.sessionCount).toBe(1);
  });

  it("keeps queue and setup waiting out of streamed totals but in their own stats", () => {
    const summary = recordPlaytimeSession(session({ seconds: 1_200, queueSeconds: 900, setupSeconds: 60 }));

    expect(summary.totals.streamedSeconds).toBe(1_200);
    expect(summary.totals.queueSeconds).toBe(900);
    expect(summary.totals.setupSeconds).toBe(60);
    expect(summary.games[0]).toMatchObject({ totalSeconds: 1_200, queueSeconds: 900, setupSeconds: 60 });
  });

  it("clamps impossible payloads and rejects a missing game id", () => {
    const before = getPlaytimeSummary();
    expect(recordPlaytimeSession(session({ gameId: "   " })).totals).toEqual(before.totals);

    const summary = recordPlaytimeSession(session({ gameId: "3003", seconds: 999_999_999, queueSeconds: -50 }));
    expect(summary.games[0].totalSeconds).toBe(86_400);
    expect(summary.games[0].queueSeconds).toBe(0);
  });

  it("buckets streamed time by day, hour, and weekday", () => {
    recordPlaytimeSession(session({
      startedAt: new Date(2026, 8, 20, 21, 15, 0).toISOString(),
      seconds: 3_600,
    }));
    recordPlaytimeSession(session({
      startedAt: new Date(2026, 8, 21, 21, 45, 0).toISOString(),
      seconds: 1_800,
    }));

    const summary = getPlaytimeSummary();
    expect(summary.hourlySeconds[21]).toBe(5_400);
    expect(summary.hourlySeconds.reduce((total, value) => total + value, 0)).toBe(5_400);
    expect(summary.weekdaySeconds.reduce((total, value) => total + value, 0)).toBe(5_400);
    expect(summary.daily).toHaveLength(2);
    expect(summary.daily[0].seconds).toBe(3_600);
    expect(summary.daily[1].sessionCount).toBe(1);
  });

  it("survives a backend restart by reloading the ledger from disk", () => {
    recordPlaytimeSession(session({ seconds: 2_400 }));
    const onDisk = JSON.parse(readFileSync(playtimeStoreInternals.filePath(), "utf8")) as {
      games: Record<string, { totalSeconds: number }>;
    };
    expect(onDisk.games["1001"].totalSeconds).toBe(2_400);

    // A fresh process reads the same file with an empty in-memory cache.
    playtimeStoreInternals.clearCache();
    expect(getPlaytimeSummary().totals.streamedSeconds).toBe(2_400);
  });

  it("recovers from a corrupted ledger instead of refusing to start", () => {
    recordPlaytimeSession(session({ seconds: 600 }));
    const path = playtimeStoreInternals.filePath();
    writeFileSync(path, "{not json");
    playtimeStoreInternals.clearCache();

    const summary = getPlaytimeSummary();
    expect(summary.totals.streamedSeconds).toBe(0);
    expect(recordPlaytimeSession(session({ seconds: 300 })).totals.streamedSeconds).toBe(300);
  });

  it("folds pre-ledger localStorage history in exactly once", () => {
    const imported = importLegacyPlaytime([
      { gameId: "1001", title: "Neon Drift", totalSeconds: 5_400, sessionCount: 4, lastPlayedAt: "2026-08-01T10:00:00.000Z" },
      { gameId: "4004", title: "Rust Belt Rally", totalSeconds: 600, sessionCount: 1 },
    ]);

    expect(imported.legacyImported).toBe(true);
    expect(imported.games.find((game) => game.gameId === "1001")).toMatchObject({
      totalSeconds: 5_400,
      sessionCount: 4,
      lastPlayedAt: "2026-08-01T10:00:00.000Z",
    });
    // Legacy rows have no trustworthy timestamps: they stay out of the recent
    // list and out of the histograms.
    expect(imported.recent).toHaveLength(0);
    expect(imported.hourlySeconds.reduce((total, value) => total + value, 0)).toBe(0);

    const second = importLegacyPlaytime([
      { gameId: "1001", title: "Neon Drift", totalSeconds: 5_400, sessionCount: 4 },
    ]);
    expect(second.games.find((game) => game.gameId === "1001")?.totalSeconds).toBe(5_400);
  });

  it("counts a live session beside imported history without double counting it", () => {
    importLegacyPlaytime([{ gameId: "1001", totalSeconds: 5_400, sessionCount: 4 }]);
    const summary = recordPlaytimeSession(session({ gameId: "1001", seconds: 1_800 }));

    expect(summary.games[0]).toMatchObject({ totalSeconds: 7_200, sessionCount: 5 });
    expect(summary.recent).toHaveLength(1);
  });

  it("caps the recent list while totals keep counting", () => {
    for (let index = 0; index < 30; index += 1) {
      recordPlaytimeSession(session({
        gameId: "1001",
        seconds: 600,
        startedAt: new Date(2026, 8, 1 + index, 12, 0, 0).toISOString(),
      }));
    }
    const summary = getPlaytimeSummary();
    expect(summary.recent).toHaveLength(24);
    expect(summary.totals.sessionCount).toBe(30);
    expect(summary.recent[0].endedAt > summary.recent[23].endedAt).toBe(true);
  });

  it("resets to an empty ledger and blocks a legacy resurrection", () => {
    recordPlaytimeSession(session({ seconds: 1_800 }));
    importLegacyPlaytime([{ gameId: "9009", totalSeconds: 3_600, sessionCount: 2 }]);

    const reset = resetPlaytime();
    expect(reset.totals.streamedSeconds).toBe(0);
    expect(reset.games).toHaveLength(0);
    expect(reset.recent).toHaveLength(0);
    expect(reset.legacyImported).toBe(true);

    playtimeStoreInternals.clearCache();
    expect(getPlaytimeSummary().totals.streamedSeconds).toBe(0);
    expect(importLegacyPlaytime([{ gameId: "9009", totalSeconds: 3_600, sessionCount: 2 }]).games).toHaveLength(0);
  });
});
