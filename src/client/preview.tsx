import { StrictMode, useMemo, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import type { GameInfo } from "@shared/gfn";
import "./styles.css";
import { MotionProvider, pageTransition } from "./components/MotionProvider";
import { AnimatePresence, m } from "motion/react";
import { SideRail } from "./components/SideRail";
import { TopHeader } from "./components/TopHeader";
import { StatusBar } from "./components/StatusBar";
import { AccountMenu } from "./components/AccountMenu";
import { HomePage } from "./components/HomePage";
import { LibraryPage } from "./components/LibraryPage";
import { StreamLoading } from "./components/StreamLoading";

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
  const [page, setPage] = useState<"home" | "library" | "settings">("library");
  const [query, setQuery] = useState("");
  const [selectedId, setSelectedId] = useState("1091500");
  const [sortId, setSortId] = useState("last_played");
  const [accountOpen, setAccountOpen] = useState(false);
  const [loadingDemo, setLoadingDemo] = useState<null | "queue" | "setup" | "connecting" | "error">("queue");
  const [demoArt, setDemoArt] = useState(true);
  const anchorRef = useRef<HTMLElement | null>(null);

  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase();
    return q ? GAMES.filter((g) => g.title.toLowerCase().includes(q)) : GAMES;
  }, [query]);

  const title = page === "home" ? "Home" : page === "library" ? "Library" : "Settings";
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
      </div>

      {loadingDemo && (
        <StreamLoading
          gameTitle="Cyberpunk 2077"
          gameCover={demoArt ? cap(1091500) : undefined}
          platformStore="STEAM"
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
