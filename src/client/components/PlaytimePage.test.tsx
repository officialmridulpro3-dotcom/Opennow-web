import type { ComponentProps } from "react";
import { describe, expect, it } from "vitest";
import { renderToStaticMarkup } from "react-dom/server";

import type { GameInfo } from "@shared/gfn";
import type { PlaytimeSummary } from "@shared/playtime";

import { PlaytimePage } from "./PlaytimePage";

const DAY = 86_400_000;

function game(id: string, title: string, store: string): GameInfo {
  return {
    id,
    title,
    imageUrl: `https://example.invalid/${id}-cover.jpg`,
    heroImageUrl: `https://example.invalid/${id}-hero.jpg`,
    availableStores: [store],
    selectedVariantIndex: 0,
    variants: [],
  } as GameInfo;
}

const GAMES = [game("1", "Neon Drift", "STEAM"), game("2", "Astro Hollow", "EPIC_GAMES_STORE"), game("3", "Rust Belt", "GOG")];

function summary(overrides: Partial<PlaytimeSummary> = {}): PlaytimeSummary {
  const now = Date.now();
  return {
    version: 1,
    updatedAt: new Date(now).toISOString(),
    legacyImported: true,
    totals: {
      streamedSeconds: 40 * 3600 + 25 * 60,
      queueSeconds: 3 * 3600 + 6 * 60,
      setupSeconds: 42 * 60,
      sessionCount: 18,
      gamesPlayed: 3,
      longestSessionSeconds: 3 * 3600 + 12 * 60,
      averageSessionSeconds: 2 * 3600 + 13 * 60,
      firstPlayedAt: new Date(now - 40 * DAY).toISOString(),
      lastPlayedAt: new Date(now - 2 * 3600_000).toISOString(),
    },
    games: [
      {
        gameId: "1",
        title: "Neon Drift",
        store: "STEAM",
        imageUrl: "https://example.invalid/1-hero.jpg",
        totalSeconds: 30 * 3600,
        queueSeconds: 2 * 3600,
        setupSeconds: 20 * 60,
        sessionCount: 10,
        longestSessionSeconds: 3 * 3600 + 12 * 60,
        firstPlayedAt: new Date(now - 40 * DAY).toISOString(),
        lastPlayedAt: new Date(now - 2 * 3600_000).toISOString(),
      },
      {
        gameId: "2",
        title: "Astro Hollow",
        store: "EPIC_GAMES_STORE",
        imageUrl: null,
        totalSeconds: 9 * 3600,
        queueSeconds: 3600,
        setupSeconds: 12 * 60,
        sessionCount: 5,
        longestSessionSeconds: 2 * 3600,
        firstPlayedAt: new Date(now - 20 * DAY).toISOString(),
        lastPlayedAt: new Date(now - 4 * DAY).toISOString(),
      },
      {
        gameId: "3",
        title: "Rust Belt",
        store: "GOG",
        imageUrl: null,
        totalSeconds: 3600 + 25 * 60,
        queueSeconds: 6 * 60,
        setupSeconds: 10 * 60,
        sessionCount: 3,
        longestSessionSeconds: 45 * 60,
        firstPlayedAt: new Date(now - 9 * DAY).toISOString(),
        lastPlayedAt: new Date(now - 8 * DAY).toISOString(),
      },
    ],
    recent: [
      {
        playbackId: "p1",
        gameId: "1",
        title: "Neon Drift",
        store: "STEAM",
        imageUrl: "https://example.invalid/1-hero.jpg",
        startedAt: new Date(now - 3 * 3600_000).toISOString(),
        endedAt: new Date(now - 2 * 3600_000).toISOString(),
        seconds: 3600,
        queueSeconds: 180,
        setupSeconds: 25,
        region: "EU-West",
        clientMode: "webrtc",
      },
      {
        playbackId: "p2",
        gameId: "2",
        title: "Astro Hollow",
        store: "EPIC_GAMES_STORE",
        imageUrl: null,
        startedAt: new Date(now - 30 * 3600_000).toISOString(),
        endedAt: new Date(now - 28 * 3600_000).toISOString(),
        seconds: 7200,
        queueSeconds: 600,
        setupSeconds: 40,
        region: "EU-West",
        clientMode: "native",
      },
    ],
    hourlySeconds: Array.from({ length: 24 }, (_, hour) => (hour === 21 ? 7200 : hour === 20 ? 3600 : 300)),
    weekdaySeconds: [5400, 1800, 1200, 1500, 2400, 4200, 6600],
    daily: Array.from({ length: 20 }, (_, index) => ({
      date: new Date(now - (19 - index) * DAY).toLocaleDateString("en-CA"),
      seconds: 1800 + index * 120,
      sessionCount: 1 + (index % 3),
    })),
    ...overrides,
  };
}

function render(overrides: Partial<ComponentProps<typeof PlaytimePage>> = {}): string {
  return renderToStaticMarkup(
    <PlaytimePage
      summary={summary()}
      libraryGames={GAMES}
      isRecording={false}
      streamStartedAtMs={null}
      recordingGameTitle={null}
      onRefreshPlaytime={async () => {}}
      onResetPlaytime={async () => {}}
      onNavigateLibrary={() => {}}
      {...overrides}
    />,
  );
}

describe("Playtime ledger page", () => {
  it("leads with the total streamed time as a display numeral", () => {
    const html = render();
    expect(html).toContain("pt-hero-numeral");
    // 40 hours exactly: the numeral shows hours, not minutes.
    expect(html).toMatch(/pt-hero-numeral[^>]*>40/);
    expect(html).toContain("pt-hero-unit");
    expect(html).toContain("pt-sr-only");
  });

  it("keeps the deck ground: mesh, scan and both blooms", () => {
    const html = render();
    for (const layer of ["pt-ambient-mesh", "pt-ambient-scan", "pt-ambient-bloom--neon", "pt-ambient-bloom--azure"]) {
      expect(html).toContain(layer);
    }
  });

  it("shows the six supporting statistics", () => {
    const html = render();
    expect(html).toContain("pt-stat-grid");
    expect((html.match(/pt-cell-head/g) ?? []).length).toBe(6);
    expect(html).toContain("18"); // sessions
    expect(html).toContain("3 h 12 m"); // longest session
    expect(html).toContain("3 h 6 m"); // queue time
  });

  it("ranks games by streamed time and resolves posters from the library", () => {
    const html = render();
    const neon = html.indexOf("Neon Drift");
    const astro = html.indexOf("Astro Hollow");
    const rust = html.indexOf("Rust Belt");
    expect(neon).toBeGreaterThan(-1);
    expect(neon).toBeLessThan(astro);
    expect(astro).toBeLessThan(rust);
    expect(html).toContain("pt-rank-index lead");
    // The leader's art comes from the library hero, not the ledger's own copy.
    expect(html).toContain("url(https://example.invalid/1-hero.jpg)");
    // Share of the total: 30h of 40h25m.
    expect(html).toContain("74% of total");
  });

  it("draws the cadence charts: 24 hours, 7 weekdays, 14 days", () => {
    const html = render();
    expect((html.match(/pt-chart-bar/g) ?? []).length).toBe(24);
    expect((html.match(/pt-week-cell/g) ?? []).length).toBe(7);
    expect((html.match(/pt-strip-cell/g) ?? []).length).toBe(14);
    expect(html).toContain('data-hour="21"');
  });

  it("lists recent sessions with the engine that played them", () => {
    const html = render();
    expect((html.match(/pt-recent-row/g) ?? []).length).toBe(2);
    expect(html).toContain("WebRTC");
    expect(html).toContain("Native");
    expect(html).toContain("10 m waiting");
  });

  it("shows the live strip only while a session is being counted", () => {
    expect(render()).not.toContain("pt-live-clock");
    const live = render({ isRecording: true, streamStartedAtMs: Date.now() - 90_000, recordingGameTitle: "Neon Drift" });
    expect(live).toContain("pt-live-clock");
    expect(live).toContain("pt-live-pulse");
    expect(live).toContain("1:30");
  });

  it("keeps the destructive reset behind a confirm step", () => {
    const html = render();
    expect(html).toContain("Reset ledger");
    expect(html).not.toContain("Erase everything");
    expect(html).not.toContain("pt-confirm");
  });

  it("offers the library when the ledger is empty", () => {
    const empty = summary({
      totals: {
        streamedSeconds: 0,
        queueSeconds: 0,
        setupSeconds: 0,
        sessionCount: 0,
        gamesPlayed: 0,
        longestSessionSeconds: 0,
        averageSessionSeconds: 0,
        firstPlayedAt: null,
        lastPlayedAt: null,
      },
      games: [],
      recent: [],
      hourlySeconds: new Array(24).fill(0),
      weekdaySeconds: new Array(7).fill(0),
      daily: [],
    });
    const html = render({ summary: empty });
    expect(html).toContain("pt-empty");
    expect(html).toContain("No playtime logged yet");
    expect(html).toContain("Browse the library");
    expect(html).not.toContain("pt-rank-row");
    // Nothing to erase yet, so no reset control either.
    expect(html).not.toContain("Reset ledger");
  });

  it("says so while the ledger is still loading", () => {
    const html = render({ summary: null });
    expect(html).toContain("Loading your ledger");
    expect(html).not.toContain("pt-hero-numeral");
  });
});
