import { StrictMode, useMemo, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import type { GameInfo } from "@shared/gfn";
import "./styles.css";
import { MotionProvider, pageTransition } from "./components/MotionProvider";
import { AnimatePresence, m } from "motion/react";
import { SideRail, type SideRailPage } from "./components/SideRail";
import { TopHeader } from "./components/TopHeader";
import { StatusBar } from "./components/StatusBar";
import { AccountMenu } from "./components/AccountMenu";
import { HomePage } from "./components/HomePage";
import { LibraryPage } from "./components/LibraryPage";
import { StreamLoading } from "./components/StreamLoading";
import { PlaytimePage } from "./components/PlaytimePage";
import type { PlaytimeGameAggregate, PlaytimeSessionRecord, PlaytimeSummary } from "@shared/playtime";

// Portrait box art via Steam's library_600x900 capsule (real posters).
const cap = (appId: number) => `https://cdn.cloudflare.steamstatic.com/steam/apps/${appId}/library_600x900.jpg`;
const hero = (appId: number) => `https://cdn.cloudflare.steamstatic.com/steam/apps/${appId}/library_hero.jpg`;

interface Seed {
  id: string;
  title: string;
  appId: number;
  store: string;
  daysAgo?: number;
  hours?: number;
}

const SEED: Seed[] = [
  { id: "1091500", title: "Cyberpunk 2077", appId: 1091500, store: "STEAM", daysAgo: 0.08, hours: 14 },
  { id: "1245620", title: "Elden Ring", appId: 1245620, store: "STEAM", daysAgo: 1, hours: 92 },
  { id: "870780", title: "Control Ultimate Edition", appId: 870780, store: "EPIC_GAMES_STORE", daysAgo: 2, hours: 21 },
  { id: "553850", title: "Helldivers 2", appId: 553850, store: "STEAM", daysAgo: 3, hours: 40 },
  { id: "1086940", title: "Baldur's Gate 3", appId: 1086940, store: "STEAM", daysAgo: 4, hours: 120 },
  { id: "782330", title: "DOOM Eternal", appId: 782330, store: "STEAM", daysAgo: 5, hours: 33 },
  { id: "1172620", title: "Sea of Thieves", appId: 1172620, store: "XBOX", daysAgo: 6, hours: 18 },
  { id: "588650", title: "Dead Cells", appId: 588650, store: "GOG", daysAgo: 8, hours: 27 },
  { id: "1085660", title: "Destiny 2", appId: 1085660, store: "STEAM", daysAgo: 10, hours: 210 },
  { id: "990080", title: "Hogwarts Legacy", appId: 990080, store: "STEAM", daysAgo: 12, hours: 46 },
  { id: "526870", title: "Satisfactory", appId: 526870, store: "EPIC_GAMES_STORE", daysAgo: 14, hours: 60 },
  { id: "2138710", title: "Marvel Rivals", appId: 2138710, store: "STEAM", daysAgo: 16, hours: 12 },
  { id: "105600", title: "Terraria", appId: 105600, store: "STEAM", daysAgo: 20, hours: 88 },
  { id: "367520", title: "Hollow Knight", appId: 367520, store: "STEAM", daysAgo: 24, hours: 54 },
  { id: "504230", title: "Celeste", appId: 504230, store: "STEAM", daysAgo: 30, hours: 22 },
  { id: "646570", title: "Slay the Spire", appId: 646570, store: "STEAM", daysAgo: 35, hours: 71 },
  { id: "413150", title: "Stardew Valley", appId: 413150, store: "STEAM", daysAgo: 40, hours: 130 },
  { id: "1174180", title: "Red Dead Redemption 2", appId: 1174180, store: "STEAM", daysAgo: 44, hours: 95 },
  { id: "292030", title: "The Witcher 3", appId: 292030, store: "GOG", daysAgo: 50, hours: 160 },
  { id: "1817070", title: "Marvel's Spider-Man", appId: 1817070, store: "STEAM", daysAgo: 60, hours: 40 },
];

function makeGame(seed: Seed, index: number): GameInfo {
  const iso = seed.daysAgo !== undefined ? new Date(Date.now() - seed.daysAgo * 86400000).toISOString() : undefined;
  return {
    id: seed.id,
    title: seed.title,
    genres: ["Action"],
    imageUrl: cap(seed.appId),
    heroImageUrl: hero(seed.appId),
    imageUrlsByType: {
      GAME_BOX_ART: [cap(seed.appId)],
      HERO_IMAGE: [hero(seed.appId)],
      KEY_ART: [hero(seed.appId)],
    },
    screenshotUrls: [hero(seed.appId)],
    availableStores: [seed.store],
    lastPlayed: iso,
    selectedVariantIndex: 0,
    variants: [
      {
        id: `${seed.id}-v`,
        store: seed.store,
        inLibrary: true,
        libraryStatus: "OWNED",
        librarySelected: true,
      } as GameInfo["variants"][number],
    ],
  } as GameInfo;
}

const GAMES = SEED.map(makeGame);
const PLAYTIME = Object.fromEntries(
  SEED.map((s) => [s.id, { lastPlayedAt: s.daysAgo !== undefined ? new Date(Date.now() - s.daysAgo * 86400000).toISOString() : null, totalSeconds: (s.hours ?? 0) * 3600 }]),
);
const FAVOURITES = ["553850", "1245620", "526870", "2138710", "105600", "367520", "504230"];

/** A plausible ledger, derived from the same seed the other pages use. */
const PLAYTIME_SUMMARY: PlaytimeSummary = (() => {
  const games: PlaytimeGameAggregate[] = SEED.map((seed) => {
    const totalSeconds = (seed.hours ?? 0) * 3600;
    const sessionCount = Math.max(1, Math.round((seed.hours ?? 0) / 2.5));
    const lastPlayedAt = seed.daysAgo !== undefined
      ? new Date(Date.now() - seed.daysAgo * 86_400_000).toISOString()
      : null;
    return {
      gameId: seed.id,
      title: seed.title,
      store: seed.store,
      imageUrl: hero(seed.appId),
      totalSeconds,
      queueSeconds: sessionCount * 95,
      setupSeconds: sessionCount * 28,
      sessionCount,
      longestSessionSeconds: Math.round((totalSeconds / sessionCount) * 2.4),
      firstPlayedAt: lastPlayedAt
        ? new Date(Date.parse(lastPlayedAt) - sessionCount * 3 * 86_400_000).toISOString()
        : null,
      lastPlayedAt,
    };
  }).sort((a, b) => b.totalSeconds - a.totalSeconds);

  const recent: PlaytimeSessionRecord[] = games.slice(0, 9).map((game, index) => {
    const endedAt = new Date(Date.now() - index * 7 * 3_600_000);
    const seconds = Math.max(600, Math.round(game.longestSessionSeconds * (0.4 + index * 0.06)));
    return {
      playbackId: `preview-${game.gameId}`,
      gameId: game.gameId,
      title: game.title,
      store: game.store,
      imageUrl: game.imageUrl,
      startedAt: new Date(endedAt.getTime() - seconds * 1000).toISOString(),
      endedAt: endedAt.toISOString(),
      seconds,
      queueSeconds: 120 + index * 35,
      setupSeconds: 22 + index * 3,
      region: "EU-West",
      clientMode: index % 3 === 0 ? "native" : "webrtc",
    };
  });

  const totals = games.reduce(
    (acc, game) => ({
      streamedSeconds: acc.streamedSeconds + game.totalSeconds,
      queueSeconds: acc.queueSeconds + game.queueSeconds,
      setupSeconds: acc.setupSeconds + game.setupSeconds,
      sessionCount: acc.sessionCount + game.sessionCount,
      longestSessionSeconds: Math.max(acc.longestSessionSeconds, game.longestSessionSeconds),
    }),
    { streamedSeconds: 0, queueSeconds: 0, setupSeconds: 0, sessionCount: 0, longestSessionSeconds: 0 },
  );

  // Evening-heavy day curve, weekend-heavy week curve.
  const hourlyShape = [2, 1, 1, 0, 0, 1, 2, 4, 6, 8, 9, 10, 12, 11, 10, 12, 16, 22, 34, 48, 58, 52, 34, 14];
  const hourlySeconds = hourlyShape.map((weight) => weight * 240);
  const weekdayShape = [46, 22, 20, 24, 28, 42, 58];
  const weekdaySeconds = weekdayShape.map((weight) => weight * 300);
  const daily = Array.from({ length: 14 }, (_, index) => {
    const date = new Date(Date.now() - (13 - index) * 86_400_000);
    const weight = 0.35 + Math.abs(Math.sin(index * 1.7)) * 1.5;
    return {
      date: date.toLocaleDateString("en-CA"),
      seconds: Math.round(weight * 5_400),
      sessionCount: Math.max(1, Math.round(weight * 2)),
    };
  });

  return {
    version: 1,
    updatedAt: new Date().toISOString(),
    legacyImported: true,
    totals: {
      ...totals,
      gamesPlayed: games.filter((game) => game.totalSeconds > 0).length,
      averageSessionSeconds: totals.sessionCount > 0
        ? Math.round(totals.streamedSeconds / totals.sessionCount)
        : 0,
      firstPlayedAt: games.map((game) => game.firstPlayedAt).filter(Boolean).sort()[0] ?? null,
      lastPlayedAt: games.map((game) => game.lastPlayedAt).filter(Boolean).sort().slice(-1)[0] ?? null,
    },
    games,
    recent,
    hourlySeconds,
    weekdaySeconds,
    daily,
  };
})();

const USER = {
  userId: "u1",
  displayName: "Zephyr",
  email: "zephyr@opennow.gg",
  membershipTier: "ULTIMATE",
};

const SORT_OPTIONS = [
  { id: "last_played", label: "Last played", orderBy: "last_played" },
  { id: "title", label: "Title (A–Z)", orderBy: "title" },
  { id: "playtime", label: "Most played", orderBy: "playtime" },
] as never[];

function PreviewApp() {
  // Phase 1 preview opens on the new Playtime ledger; the rail still reaches
  // home, library and the queue deck demo.
  const [page, setPage] = useState<SideRailPage>("playtime");
  const [query, setQuery] = useState("");
  const [selectedId, setSelectedId] = useState("1091500");
  const [sortId, setSortId] = useState("last_played");
  const [accountOpen, setAccountOpen] = useState(false);
  const [loadingDemo, setLoadingDemo] = useState<null | "queue" | "setup" | "connecting" | "error">(null);
  const [demoArt, setDemoArt] = useState(true);
  const [demoAds, setDemoAds] = useState(false);
  const [demoLive, setDemoLive] = useState(true);
  // A stream that started 21 minutes ago, so the live strip has a real clock.
  const liveStartedAtMs = useMemo(() => Date.now() - 21 * 60_000, []);
  const anchorRef = useRef<HTMLElement | null>(null);

  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase();
    return q ? GAMES.filter((g) => g.title.toLowerCase().includes(q)) : GAMES;
  }, [query]);

  const title = page === "home"
    ? "Home"
    : page === "library"
      ? "Library"
      : page === "playtime"
        ? "Playtime"
        : "Settings";
  const count = GAMES.length;

  return (
    <div className="app-container app-shell app-container--atmosphere">
      <SideRail
        currentPage={page}
        onNavigate={(p) => setPage(p)}
        user={USER as never}
        onOpenAccount={() => setAccountOpen((o) => !o)}
        avatarRef={anchorRef as never}
      />
      <AccountMenu
        open={accountOpen}
        onClose={() => setAccountOpen(false)}
        anchorRef={anchorRef}
        user={USER as never}
        subscription={{
          membershipTier: "Ultimate",
          allottedHours: 100,
          purchasedHours: 0,
          rolledOverHours: 0,
          usedHours: 42,
          remainingHours: 58,
          totalHours: 100,
          isUnlimited: false,
          entitledResolutions: [],
          storageAddon: { sizeGb: 200, usedGb: 84, regionName: "EU" },
        } as never}
        activeSession={null}
        activeSessionGameTitle={null}
        isResumingSession={false}
        isTerminatingSession={false}
        onResumeSession={() => {}}
        onTerminateSession={() => {}}
        savedAccounts={[
          { userId: "u1", displayName: "Zephyr", email: "zephyr@opennow.gg", membershipTier: "ULTIMATE", providerCode: "NVIDIA" },
          { userId: "u2", displayName: "Nova", email: "nova@opennow.gg", membershipTier: "PRIORITY", providerCode: "NVIDIA" },
        ]}
        onSwitchAccount={() => {}}
        onRemoveAccount={() => {}}
        onAddAccount={() => {}}
        onLogoutAll={() => {}}
      />

      <div className="app-shell-main">
        <TopHeader
          title={title}
          count={count}
          countLabel={`${count} games`}
          searchQuery={query}
          onSearchChange={setQuery}
          searchPlaceholder={page === "library" ? "Search your library..." : "Search games..."}
          hideSearch={page === "playtime"}
        />
        <main className="main-content">
          <AnimatePresence mode="wait" initial={false}>
            <m.div
              key={page}
              className="page-transition-surface"
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
              exit={{ opacity: 0 }}
              transition={pageTransition}
            >
              {page === "home" && (
                <HomePage
                  games={filtered}
                  searchQuery={query}
                  onSearchChange={setQuery}
                  onPlayGame={() => {}}
                  isLoading={false}
                  selectedGameId={selectedId}
                  onSelectGame={setSelectedId}
                  selectedVariantByGameId={{}}
                  onSelectGameVariant={() => {}}
                  filterGroups={[]}
                  selectedFilterIds={[]}
                  onToggleFilter={() => {}}
                  sortOptions={SORT_OPTIONS}
                  selectedSortId={sortId}
                  onSortChange={setSortId}
                  totalCount={count}
                  supportedCount={count}
                  libraryGames={GAMES}
                  playtimeData={PLAYTIME}
                  favoriteGameIds={FAVOURITES}
                  streamMetaLabel="1440p · 120 fps · AV1"
                  onNavigateLibrary={() => setPage("library")}
                  onNavigatePlaytime={() => setPage("playtime")}
                />
              )}
              {page === "library" && (
                <LibraryPage
                  games={filtered}
                  allGames={GAMES}
                  playtimeData={PLAYTIME}
                  searchQuery={query}
                  onSearchChange={setQuery}
                  onPlayGame={() => {}}
                  isLoading={false}
                  selectedGameId={selectedId}
                  onSelectGame={setSelectedId}
                  selectedVariantByGameId={{}}
                  onSelectGameVariant={() => {}}
                  libraryCount={GAMES.length}
                  sortOptions={SORT_OPTIONS}
                  selectedSortId={sortId}
                  onSortChange={setSortId}
                  featuredGames={GAMES}
                  onNavigatePlaytime={() => setPage("playtime")}
                />
              )}
              {page === "playtime" && (
                <PlaytimePage
                  summary={PLAYTIME_SUMMARY}
                  libraryGames={GAMES}
                  isRecording={demoLive}
                  streamStartedAtMs={demoLive ? liveStartedAtMs : null}
                  recordingGameTitle="Cyberpunk 2077"
                  onRefreshPlaytime={async () => {}}
                  onResetPlaytime={async () => {}}
                  onNavigateLibrary={() => setPage("library")}
                />
              )}
              {page === "settings" && (
                <div style={{ padding: 40, color: "var(--ink-soft)" }}>Settings preview not included.</div>
              )}
            </m.div>
          </AnimatePresence>
        </main>
        <StatusBar />
      </div>

      {/* Queue / connecting screen demo controls */}
      <div style={{ position: "fixed", top: 14, right: 18, zIndex: 3000, display: "flex", gap: 8 }}>
        {(["queue", "setup", "connecting", "error"] as const).map((s) => (
          <button
            key={s}
            type="button"
            onClick={() => setLoadingDemo(s)}
            style={{
              height: 30, padding: "0 12px", borderRadius: 999, cursor: "pointer",
              border: "1px solid rgba(255,255,255,0.16)", background: "rgba(20,22,26,0.8)",
              color: "#fff", fontSize: 12, fontWeight: 700, fontFamily: "inherit", textTransform: "capitalize",
            }}
          >
            {s}
          </button>
        ))}
        <button
          type="button"
          onClick={() => setDemoArt((a) => !a)}
          title="Toggle key art to preview the no-cover fallback"
          style={{
            height: 30, padding: "0 12px", borderRadius: 999, cursor: "pointer",
            border: "1px solid rgba(255,255,255,0.16)", background: "rgba(20,22,26,0.8)",
            color: "#fff", fontSize: 12, fontWeight: 700, fontFamily: "inherit",
          }}
        >
          {demoArt ? "art: on" : "art: off"}
        </button>
        <button
          type="button"
          onClick={() => setDemoLive((l) => !l)}
          title="Toggle the live session strip on the Playtime page"
          style={{
            height: 30, padding: "0 12px", borderRadius: 999, cursor: "pointer",
            border: "1px solid rgba(255,255,255,0.16)", background: "rgba(20,22,26,0.8)",
            color: "#fff", fontSize: 12, fontWeight: 700, fontFamily: "inherit",
          }}
        >
          {demoLive ? "live: on" : "live: off"}
        </button>
        <button
          type="button"
          onClick={() => setDemoAds((a) => !a)}
          title="Hold the queue for an ad to preview how the body line absorbs it"
          style={{
            height: 30, padding: "0 12px", borderRadius: 999, cursor: "pointer",
            border: "1px solid rgba(255,255,255,0.16)", background: "rgba(20,22,26,0.8)",
            color: "#fff", fontSize: 12, fontWeight: 700, fontFamily: "inherit",
          }}
        >
          {demoAds ? "ads: on" : "ads: off"}
        </button>
      </div>

      {loadingDemo && (
        <StreamLoading
          gameTitle="Cyberpunk 2077"
          gameCover={demoArt ? cap(1091500) : undefined}
          gameHero={demoArt ? hero(1091500) : undefined}
          platformStore="STEAM"
          adState={
            demoAds
              ? ({
                  isAdsRequired: true,
                  message: "Watch to keep your place in line",
                  sessionAds: [{ adId: "demo-ad", title: "Sponsored break" }],
                  ads: [],
                } as never)
              : undefined
          }
          status={loadingDemo === "error" ? "queue" : loadingDemo}
          queuePosition={loadingDemo === "queue" ? 42 : undefined}
          estimatedWait={loadingDemo === "queue" ? "3 min" : undefined}
          diagnosticLine={
            loadingDemo === "queue"
              ? "poll #12 · seat status 1 · setup step n/a · queue 42 · endpoints 0"
              : undefined
          }
          error={
            loadingDemo === "error"
              ? {
                  title: "No cloud rig available",
                  description:
                    "Every rig in Europe West is busy right now, so your place in the queue was released. Try again in a few minutes.",
                  code: "ERR_NO_CAPACITY",
                  actionLabel: "Try again",
                }
              : undefined
          }
          onErrorAction={() => setLoadingDemo("queue")}
          onCancel={() => setLoadingDemo(null)}
        />
      )}
    </div>
  );
}

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <MotionProvider>
      <PreviewApp />
    </MotionProvider>
  </StrictMode>,
);
