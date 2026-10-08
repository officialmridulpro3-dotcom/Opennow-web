import { Library, Search, Gamepad2, ArrowUpDown, Play, Timer, Clock, ChevronDown } from "lucide-react";
import { memo, useEffect, useMemo, useRef, useState } from "react";
import type { JSX } from "react";
import { AnimatePresence } from "motion/react";
import type { CatalogSortOption, GameInfo } from "@shared/gfn";
import { PosterCard } from "./PosterCard";
import { getStoreDisplayName, getStoreIconComponent } from "./GameCard";
import type { PlaytimeData } from "../lib/gameCatalog";
import { getControllerFeaturedGames } from "../lib/controllerCatalogUi";
import {
  gameMatchesLibraryFilters,
  gameMatchesStoreFilter,
  getControllerStoreFilterItems,
  getLibraryFilterGroups,
  getLibraryFilterOptionById,
  type ControllerStoreFilterItem,
  type LibraryFilterOption,
} from "../lib/libraryFilters";
import { useTranslation } from "../i18n";
import { formatCatalogLastPlayed } from "../utils/lastPlayedFormat";
import { formatPlaytimeDuration } from "../utils/playtimeFormat";
import { controllerButton, readControllerGamepadButtons } from "../utils/controllerGamepad";
import { SelectDropdown } from "./ui/SelectDropdown";
import { LibraryControllerView } from "./library/LibraryControllerView";
import { MotionSpinner } from "./MotionSpinner";

const INITIAL_LIBRARY_PAGE_SIZE = 48;
const LIBRARY_PAGE_SIZE = 48;
const CONTROLLER_HERO_ROTATION_MS = 8000;
const CONTROLLER_MOVE_REPEAT_MS = 140;
const CONTROLLER_Y_HOLD_MS = 350;

function getPrimaryGenre(game: GameInfo): string {
  return game.genres?.[0] ?? game.playType ?? "";
}

export interface LibraryPageProps {
  games: GameInfo[];
  allGames: GameInfo[];
  playtimeData: PlaytimeData;
  searchQuery: string;
  onSearchChange: (query: string) => void;
  onPlayGame: (game: GameInfo) => void;
  onBuyGame?: (game: GameInfo, selectedVariantId?: string) => void;
  isLoading: boolean;
  selectedGameId: string;
  onSelectGame: (id: string) => void;
  selectedVariantByGameId: Record<string, string>;
  onSelectGameVariant: (gameId: string, variantId: string) => void;
  libraryCount: number;
  sortOptions: CatalogSortOption[];
  selectedSortId: string;
  onSortChange: (sortId: string) => void;
  controllerMode?: boolean;
  featuredGames?: GameInfo[];
  activeSessionAppIds?: number[];
  onPreviousControllerPage?: () => void;
  onNextControllerPage?: () => void;
  onNavigatePlaytime?: () => void;
}

export const LibraryPage = memo(function LibraryPage({
  games,
  allGames,
  playtimeData,
  searchQuery,
  onSearchChange,
  onPlayGame,
  onBuyGame,
  isLoading,
  selectedGameId,
  onSelectGame,
  selectedVariantByGameId,
  onSelectGameVariant,
  libraryCount,
  sortOptions,
  selectedSortId,
  onSortChange,
  controllerMode = false,
  featuredGames = [],
  activeSessionAppIds = [],
  onPreviousControllerPage,
  onNextControllerPage,
  onNavigatePlaytime,
}: LibraryPageProps): JSX.Element {
  const { t } = useTranslation();
  const [controllerHeroIndex, setControllerHeroIndex] = useState(0);
  const [detailsGame, setDetailsGame] = useState<GameInfo | null>(null);
  const [controllerStoreFilterId, setControllerStoreFilterId] = useState("library");
  const [controllerStoreFilterOpen, setControllerStoreFilterOpen] = useState(false);
  const [controllerSearchOpen, setControllerSearchOpen] = useState(false);
  const [focusedControllerStoreFilterIndex, setFocusedControllerStoreFilterIndex] = useState(0);
  const [selectedLibraryFilterIds, setSelectedLibraryFilterIds] = useState<string[]>([]);
  const [libraryVisibleCount, setLibraryVisibleCount] = useState(INITIAL_LIBRARY_PAGE_SIZE);
  const controllerSearchInputRef = useRef<HTMLInputElement | null>(null);
  const gamepadPreviousButtonsRef = useRef(0);
  const gamepadLastMoveAtRef = useRef(0);
  const gamepadFrameRef = useRef<number | null>(null);
  const controllerYPressedAtRef = useRef(0);
  const controllerYConsumedByHoldRef = useRef(false);
  const controllerGameRowRef = useRef<HTMLDivElement | null>(null);
  const controllerInputStateRef = useRef({
    detailsGame: null as GameInfo | null,
    selectedControllerGame: undefined as GameInfo | undefined,
    selectedControllerGameIndex: 0,
    controllerStoreFilterOpen: false,
    focusedControllerStoreFilterIndex: 0,
    controllerStoreFilterItems: [] as ControllerStoreFilterItem[],
    focusControllerGame: (_index: number): void => {},
    cycleSelectedVariant: (): void => {},
    cycleControllerStoreFilter: (): void => {},
    moveControllerStoreFilterFocusBy: (_delta: number): void => {},
    hideControllerStoreFilterOverlay: (_applySelection: boolean): void => {},
    showControllerStoreFilterOverlay: (): void => {},
    onPlayGame: (_game: GameInfo): void => {},
  });

  useEffect(() => {
    if (!controllerMode || !controllerSearchOpen) return;
    controllerSearchInputRef.current?.focus();
  }, [controllerMode, controllerSearchOpen]);

  const librarySearchHasQuery = searchQuery.trim().length > 0;
  const libraryFilterGroups = useMemo(
    () => getLibraryFilterGroups(allGames, playtimeData, t),
    [allGames, playtimeData, t],
  );
  const visibleLibraryGames = useMemo(
    () => games.filter((game) => gameMatchesLibraryFilters(game, selectedLibraryFilterIds, playtimeData, t)),
    [games, playtimeData, selectedLibraryFilterIds, t],
  );
  const pagedLibraryGames = useMemo(
    () => visibleLibraryGames.slice(0, libraryVisibleCount),
    [libraryVisibleCount, visibleLibraryGames],
  );

  useEffect(() => {
    setLibraryVisibleCount(INITIAL_LIBRARY_PAGE_SIZE);
  }, [searchQuery, selectedLibraryFilterIds, selectedSortId]);

  const activeLibraryFilterOptions = useMemo(
    () => selectedLibraryFilterIds
      .map((filterId) => getLibraryFilterOptionById(libraryFilterGroups, filterId))
      .filter((option): option is LibraryFilterOption => Boolean(option)),
    [libraryFilterGroups, selectedLibraryFilterIds],
  );
  const hasActiveLibraryFilters = activeLibraryFilterOptions.length > 0;

  useEffect(() => {
    const availableFilterIds = new Set(libraryFilterGroups.flatMap((group) => group.options.map((option) => option.id)));
    setSelectedLibraryFilterIds((previous) => {
      const next = previous.filter((filterId) => availableFilterIds.has(filterId));
      return next.length === previous.length ? previous : next;
    });
  }, [libraryFilterGroups]);

  useEffect(() => {
    if (controllerMode || visibleLibraryGames.length === 0) return;
    if (visibleLibraryGames.some((game) => game.id === selectedGameId)) return;
    onSelectGame(visibleLibraryGames[0].id);
  }, [controllerMode, onSelectGame, selectedGameId, visibleLibraryGames]);

  const toggleLibraryFilter = (filterId: string): void => {
    setSelectedLibraryFilterIds((previous) =>
      previous.includes(filterId)
        ? previous.filter((selectedFilterId) => selectedFilterId !== filterId)
        : [...previous, filterId],
    );
  };

  const clearLibraryFilters = (): void => {
    setSelectedLibraryFilterIds([]);
  };

  const controllerStoreFilterItems = useMemo(
    () => getControllerStoreFilterItems(games, t("library.allStores")),
    [games, t],
  );
  const controllerGames = useMemo(
    () => (controllerStoreFilterId === "library" ? games : games.filter((game) => gameMatchesStoreFilter(game, controllerStoreFilterId))),
    [controllerStoreFilterId, games],
  );
  const controllerFeaturedGames = useMemo(
    () => getControllerFeaturedGames(featuredGames, controllerGames),
    [featuredGames, controllerGames],
  );

  useEffect(() => {
    if (!controllerMode) return;
    setControllerHeroIndex(0);
  }, [controllerMode, controllerStoreFilterId, controllerFeaturedGames.length, controllerFeaturedGames[0]?.id]);

  useEffect(() => {
    if (controllerMode) return;
    gamepadPreviousButtonsRef.current = 0;
    gamepadLastMoveAtRef.current = 0;
  }, [controllerMode]);

  useEffect(() => {
    if (!controllerMode || controllerFeaturedGames.length <= 1) return;
    const interval = window.setInterval(() => {
      setControllerHeroIndex((index) => (index + 1) % controllerFeaturedGames.length);
    }, CONTROLLER_HERO_ROTATION_MS);
    return () => window.clearInterval(interval);
  }, [controllerMode, controllerFeaturedGames.length]);

  useEffect(() => {
    if (!controllerMode || games.length === 0) return;
    if (controllerGames.some((game) => game.id === selectedGameId)) return;
    onSelectGame(controllerGames[0]?.id ?? games[0].id);
  }, [controllerGames, controllerMode, games, onSelectGame, selectedGameId]);

  useEffect(() => {
    if (controllerStoreFilterItems.some((item) => item.id === controllerStoreFilterId)) return;
    setControllerStoreFilterId("library");
    setFocusedControllerStoreFilterIndex(0);
  }, [controllerStoreFilterId, controllerStoreFilterItems]);

  const selectedControllerGameIndex = Math.max(0, controllerGames.findIndex((game) => game.id === selectedGameId));
  const selectedControllerGame = controllerGames[selectedControllerGameIndex] ?? controllerGames[0];

  const focusControllerGame = (index: number): void => {
    if (controllerGames.length === 0) return;
    const nextIndex = Math.max(0, Math.min(index, controllerGames.length - 1));
    const nextGame = controllerGames[nextIndex];
    onSelectGame(nextGame.id);
    window.requestAnimationFrame(() => {
      const row = controllerGameRowRef.current;
      const card = row?.querySelector<HTMLElement>(`[data-controller-game-id="${CSS.escape(nextGame.id)}"]`);
      card?.scrollIntoView({ inline: "nearest", block: "nearest", behavior: "auto" });
    });
  };

  const cycleGameVariant = (game: GameInfo | undefined): void => {
    if (!game || game.variants.length <= 1) return;
    const activeVariantId = selectedVariantByGameId[game.id];
    const activeIndex = Math.max(0, game.variants.findIndex((variant) => variant.id === activeVariantId));
    const nextVariant = game.variants[(activeIndex + 1) % game.variants.length];
    if (nextVariant) onSelectGameVariant(game.id, nextVariant.id);
  };

  const cycleSelectedVariant = (): void => {
    cycleGameVariant(selectedControllerGame);
  };

  const cycleControllerStoreFilter = (): void => {
    if (controllerStoreFilterItems.length <= 1) return;
    const activeIndex = Math.max(0, controllerStoreFilterItems.findIndex((item) => item.id === controllerStoreFilterId));
    const nextItem = controllerStoreFilterItems[(activeIndex + 1) % controllerStoreFilterItems.length];
    setControllerStoreFilterId(nextItem.id);
    setFocusedControllerStoreFilterIndex((activeIndex + 1) % controllerStoreFilterItems.length);
    setControllerHeroIndex(0);
  };

  const showControllerStoreFilterOverlay = (): void => {
    const activeIndex = Math.max(0, controllerStoreFilterItems.findIndex((item) => item.id === controllerStoreFilterId));
    setFocusedControllerStoreFilterIndex(activeIndex);
    setControllerStoreFilterOpen(true);
  };

  const moveControllerStoreFilterFocusBy = (delta: number): void => {
    if (controllerStoreFilterItems.length === 0) return;
    setFocusedControllerStoreFilterIndex((index) => Math.max(0, Math.min(index + delta, controllerStoreFilterItems.length - 1)));
  };

  const hideControllerStoreFilterOverlay = (applySelection: boolean): void => {
    if (applySelection) {
      const item = controllerStoreFilterItems[focusedControllerStoreFilterIndex] ?? controllerStoreFilterItems[0];
      if (item) {
        setControllerStoreFilterId(item.id);
        setControllerHeroIndex(0);
      }
    }
    setControllerStoreFilterOpen(false);
  };

  controllerInputStateRef.current = {
    detailsGame,
    selectedControllerGame,
    selectedControllerGameIndex,
    controllerStoreFilterOpen,
    focusedControllerStoreFilterIndex,
    controllerStoreFilterItems,
    focusControllerGame,
    cycleSelectedVariant,
    cycleControllerStoreFilter,
    moveControllerStoreFilterFocusBy,
    hideControllerStoreFilterOverlay,
    showControllerStoreFilterOverlay,
    onPlayGame,
  };

  useEffect(() => {
    if (!controllerMode) return;
    const handleKeyDown = (event: KeyboardEvent) => {
      if (detailsGame) {
        if (event.key === "Escape" || event.key.toLowerCase() === "b") {
          event.preventDefault();
          setDetailsGame(null);
        }
        if (event.key === "Enter" || event.key === " ") {
          event.preventDefault();
          onPlayGame(detailsGame);
        }
        return;
      }
      if (controllerSearchOpen) {
        if (event.key === "Escape") {
          event.preventDefault();
          setControllerSearchOpen(false);
        }
        return;
      }
      if (event.key === "ArrowLeft") {
        event.preventDefault();
        focusControllerGame(selectedControllerGameIndex - 1);
      } else if (event.key === "ArrowRight") {
        event.preventDefault();
        focusControllerGame(selectedControllerGameIndex + 1);
      } else if (event.key === "ArrowDown") {
        event.preventDefault();
        cycleSelectedVariant();
      } else if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        if (selectedControllerGame) onPlayGame(selectedControllerGame);
      } else if (event.key.toLowerCase() === "x") {
        event.preventDefault();
        setControllerSearchOpen(true);
      } else if (event.key.toLowerCase() === "b" || event.key === "Escape") {
        event.preventDefault();
        onPreviousControllerPage?.();
      } else if (event.key === "[") {
        event.preventDefault();
        onPreviousControllerPage?.();
      } else if (event.key === "]") {
        event.preventDefault();
        onNextControllerPage?.();
      } else if (event.key.toLowerCase() === "i" || event.key.toLowerCase() === "m") {
        event.preventDefault();
        if (selectedControllerGame) setDetailsGame(selectedControllerGame);
      }
    };
    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, [controllerMode, controllerSearchOpen, detailsGame, onNextControllerPage, onPlayGame, onPreviousControllerPage, selectedControllerGame, selectedControllerGameIndex]);

  useEffect(() => {
    if (!controllerMode) return;
    const readButtons = (): number => {
      const pad = navigator.getGamepads?.().find((gamepad): gamepad is Gamepad => Boolean(gamepad));
      return readControllerGamepadButtons(pad);
    };

    const handleGamepadFrame = () => {
      const buttons = readButtons();
      let pressed = buttons & ~gamepadPreviousButtonsRef.current;
      const released = gamepadPreviousButtonsRef.current & ~buttons;
      const moveMask = controllerButton.up | controllerButton.down | controllerButton.left | controllerButton.right;
      const yButton = controllerButton.north;
      const now = performance.now();
      const activeMoves = buttons & moveMask;
      const pressedMoves = pressed & moveMask;
      if (pressedMoves) {
        gamepadLastMoveAtRef.current = now;
      } else if (activeMoves && now - gamepadLastMoveAtRef.current > CONTROLLER_MOVE_REPEAT_MS) {
        pressed |= activeMoves;
        gamepadLastMoveAtRef.current = now;
      }

      const {
        detailsGame: currentDetailsGame,
        selectedControllerGame: currentSelectedGame,
        selectedControllerGameIndex: currentSelectedIndex,
        controllerStoreFilterOpen: storeFilterOpen,
        focusControllerGame: focusGame,
        cycleSelectedVariant: cycleVariant,
        cycleControllerStoreFilter: cycleStoreFilter,
        moveControllerStoreFilterFocusBy: moveStoreFilter,
        hideControllerStoreFilterOverlay: hideStoreFilter,
        showControllerStoreFilterOverlay: showStoreFilter,
        onPlayGame: playGame,
      } = controllerInputStateRef.current;

      if (pressed & yButton) {
        controllerYPressedAtRef.current = now;
        controllerYConsumedByHoldRef.current = false;
      }

      if ((buttons & yButton) && !controllerYConsumedByHoldRef.current && now - controllerYPressedAtRef.current >= CONTROLLER_Y_HOLD_MS) {
        controllerYConsumedByHoldRef.current = true;
        showStoreFilter();
      }

      if (controllerSearchOpen) {
        if (pressed & controllerButton.east) setControllerSearchOpen(false);
        gamepadPreviousButtonsRef.current = buttons;
        gamepadFrameRef.current = window.requestAnimationFrame(handleGamepadFrame);
        return;
      }

      if (storeFilterOpen) {
        if (pressed & controllerButton.up) moveStoreFilter(-1);
        if (pressed & controllerButton.down) moveStoreFilter(1);
        if (pressed & controllerButton.east) hideStoreFilter(false);
        if (released & yButton) hideStoreFilter(true);
        gamepadPreviousButtonsRef.current = buttons;
        gamepadFrameRef.current = window.requestAnimationFrame(handleGamepadFrame);
        return;
      }

      if (currentDetailsGame) {
        if (pressed & controllerButton.south) playGame(currentDetailsGame);
        if (pressed & controllerButton.east) setDetailsGame(null);
      } else {
        if ((released & yButton) && !controllerYConsumedByHoldRef.current) cycleStoreFilter();
        if (pressed & controllerButton.south) {
          if (currentSelectedGame) playGame(currentSelectedGame);
        }
        if (pressed & controllerButton.east) onPreviousControllerPage?.();
        if (pressed & controllerButton.west) setControllerSearchOpen(true);
        if (pressed & controllerButton.leftShoulder) onPreviousControllerPage?.();
        if (pressed & controllerButton.rightShoulder) onNextControllerPage?.();
        if (pressed & controllerButton.menu) {
          if (currentSelectedGame) setDetailsGame(currentSelectedGame);
        }
        if (pressed & controllerButton.left) focusGame(currentSelectedIndex - 1);
        if (pressed & controllerButton.right) focusGame(currentSelectedIndex + 1);
        if (pressed & controllerButton.down) cycleVariant();
      }
      gamepadPreviousButtonsRef.current = buttons;
      gamepadFrameRef.current = window.requestAnimationFrame(handleGamepadFrame);
    };

    const startGamepadNavigation = () => {
      if (gamepadFrameRef.current !== null) return;
      gamepadPreviousButtonsRef.current = readButtons();
      gamepadLastMoveAtRef.current = performance.now();
      gamepadFrameRef.current = window.requestAnimationFrame(handleGamepadFrame);
    };

    const stopGamepadNavigation = () => {
      if (gamepadFrameRef.current !== null) {
        window.cancelAnimationFrame(gamepadFrameRef.current);
        gamepadFrameRef.current = null;
      }
      gamepadPreviousButtonsRef.current = 0;
      gamepadLastMoveAtRef.current = 0;
    };

    const handleDisconnect = () => {
      const hasConnectedPad = navigator.getGamepads?.().some(Boolean) ?? false;
      if (!hasConnectedPad) stopGamepadNavigation();
    };

    window.addEventListener("gamepadconnected", startGamepadNavigation);
    window.addEventListener("gamepaddisconnected", handleDisconnect);
    startGamepadNavigation();

    return () => {
      window.removeEventListener("gamepadconnected", startGamepadNavigation);
      window.removeEventListener("gamepaddisconnected", handleDisconnect);
      stopGamepadNavigation();
    };
  }, [controllerMode, controllerSearchOpen, onNextControllerPage, onPreviousControllerPage]);

  // ---- immersive desktop library ----

  const selectedGame = useMemo(() => {
    return (
      visibleLibraryGames.find((g) => g.id === selectedGameId) ??
      allGames.find((g) => g.id === selectedGameId) ??
      visibleLibraryGames[0] ??
      allGames[0] ??
      null
    );
  }, [allGames, selectedGameId, visibleLibraryGames]);

  const selectedHero = selectedGame
    ? selectedGame.heroImageUrl ??
      selectedGame.imageUrlsByType?.HERO_IMAGE?.[0] ??
      selectedGame.imageUrlsByType?.MARQUEE_HERO_IMAGE?.[0] ??
      selectedGame.imageUrlsByType?.KEY_ART?.[0] ??
      selectedGame.screenshotUrls?.[0] ??
      selectedGame.imageUrl
    : undefined;

  const selectedVariant = selectedGame
    ? selectedGame.variants[selectedGame.selectedVariantIndex] ?? selectedGame.variants[0]
    : undefined;
  const selectedStoreRaw = selectedVariant?.store ?? selectedGame?.availableStores?.[0];
  const SelectedStoreIcon = selectedStoreRaw ? getStoreIconComponent(selectedStoreRaw) : null;
  const selectedStoreName = selectedStoreRaw ? getStoreDisplayName(selectedStoreRaw) : "";
  const selectedGenre = selectedGame ? getPrimaryGenre(selectedGame) : "";
  const selectedPlay = selectedGame ? playtimeData[selectedGame.id] : undefined;
  const selectedSeconds = selectedPlay?.totalSeconds ?? 0;
  const selectedSessions = selectedPlay?.sessionCount ?? 0;
  const selectedLastPlayedIso = selectedPlay?.lastPlayedAt ?? selectedGame?.lastPlayed;
  const selectedLastPlayed = formatCatalogLastPlayed(t, selectedLastPlayedIso ?? undefined);
  const selectedPlaytime = formatPlaytimeDuration(t, selectedSeconds);

  const detailCells = [
    { id: "lastPlayed", label: t("library.deck.cellLastPlayed"), value: selectedLastPlayed },
    { id: "playtime", label: t("library.deck.cellPlaytime"), value: selectedSeconds > 0 ? selectedPlaytime : t("playtime.format.never") },
    {
      id: "sessions",
      label: t("library.deck.cellSessions"),
      value: selectedSessions > 0 ? t("playtime.ranking.sessions", { count: selectedSessions }) : "",
    },
    { id: "store", label: t("library.deck.cellStore"), value: selectedStoreName, icon: SelectedStoreIcon },
    { id: "genre", label: t("library.deck.cellGenre"), value: selectedGenre },
  ].filter((c) => Boolean(c.value));

  const libraryGridItems = useMemo(
    () => pagedLibraryGames.map((game) => {
      const seconds = playtimeData[game.id]?.totalSeconds ?? 0;
      const note = seconds > 0
        ? formatPlaytimeDuration(t, seconds)
        : formatCatalogLastPlayed(t, playtimeData[game.id]?.lastPlayedAt ?? game.lastPlayed ?? undefined);
      return (
        <PosterCard
          key={game.id}
          game={game}
          isSelected={game.id === selectedGameId}
          onSelect={() => onSelectGame(game.id)}
          onPlay={() => onPlayGame(game)}
          note={note}
        />
      );
    }),
    [onPlayGame, onSelectGame, pagedLibraryGames, playtimeData, selectedGameId, t],
  );

  if (controllerMode) {
    const featuredGame = controllerFeaturedGames[controllerHeroIndex] ?? selectedControllerGame;
    return (
      <LibraryControllerView
        isLoading={isLoading}
        libraryCount={libraryCount}
        searchQuery={searchQuery}
        onSearchChange={onSearchChange}
        selectedGameId={selectedGameId}
        onSelectGame={onSelectGame}
        onPlayGame={onPlayGame}
        onBuyGame={onBuyGame}
        selectedVariantByGameId={selectedVariantByGameId}
        activeSessionAppIds={activeSessionAppIds}
        featuredGame={featuredGame}
        controllerFeaturedGames={controllerFeaturedGames}
        controllerHeroIndex={controllerHeroIndex}
        controllerGames={controllerGames}
        controllerGameRowRef={controllerGameRowRef}
        controllerStoreFilterOpen={controllerStoreFilterOpen}
        controllerStoreFilterItems={controllerStoreFilterItems}
        focusedControllerStoreFilterIndex={focusedControllerStoreFilterIndex}
        onFocusControllerStoreFilter={setFocusedControllerStoreFilterIndex}
        onSelectControllerStoreFilter={(itemId) => {
          setControllerStoreFilterId(itemId);
          setControllerStoreFilterOpen(false);
        }}
        controllerSearchOpen={controllerSearchOpen}
        controllerSearchInputRef={controllerSearchInputRef}
        detailsGame={detailsGame}
        onCloseDetails={() => setDetailsGame(null)}
        onCycleGameVariant={cycleGameVariant}
      />
    );
  }

  const collectionChips = libraryFilterGroups.flatMap((group) =>
    group.options.map((option) => ({ ...option, groupId: group.id })),
  );

  return (
    <div className="library-page library-page--deck">
      <div className="lib-layout">
        <div className="lib-main">
          <div className="lib-head">
            <h2 className="lib-title">
              {hasActiveLibraryFilters ? activeLibraryFilterOptions[0].label : t("library.deck.allGames")}
            </h2>
            <span className="lib-title-rule" aria-hidden="true" />
            <span className="lib-count">
              {t("library.deck.count", { count: visibleLibraryGames.length })}
            </span>
            <div className="lib-head-actions">
              {sortOptions.length > 0 && (
                <div className="lib-sort">
                  <ArrowUpDown size={14} />
                  <SelectDropdown
                    value={selectedSortId}
                    options={sortOptions.map((option) => ({ value: option.id, label: option.label }))}
                    onChange={onSortChange}
                    ariaLabel={t("library.sortAriaLabel")}
                  />
                </div>
              )}
            </div>
          </div>

          <div className="lib-collections">
            <div className="lib-collections-chips">
              <button
                type="button"
                className={`lib-chip${!hasActiveLibraryFilters ? " active" : ""}`}
                onClick={clearLibraryFilters}
              >
                <span>{t("library.deck.chipAll")}</span>
                <span className="lib-chip-count">{libraryCount}</span>
              </button>
              {collectionChips.map((chip) => {
                const active = selectedLibraryFilterIds.includes(chip.id);
                return (
                  <button
                    key={chip.id}
                    type="button"
                    className={`lib-chip${active ? " active" : ""}`}
                    onClick={() => toggleLibraryFilter(chip.id)}
                    aria-pressed={active}
                  >
                    <span>{chip.label}</span>
                    <span className="lib-chip-count">{chip.count}</span>
                  </button>
                );
              })}
            </div>
            <span className="lib-collections-hint">{t("library.deck.hint")}</span>
          </div>

          <div className="lib-grid-area">
            {isLoading ? (
              <div className="lib-empty">
                <MotionSpinner className="lib-spinner" size={36} label={t("common.loading")} />
                <p>{t("library.empty.loadingLibrary")}</p>
              </div>
            ) : libraryCount === 0 ? (
              <div className="lib-empty">
                <Gamepad2 className="lib-empty-icon" size={44} />
                <h3>{t("library.empty.libraryEmpty")}</h3>
                <p>{t("library.empty.ownedGamesAppearHere")}</p>
              </div>
            ) : visibleLibraryGames.length === 0 ? (
              <div className="lib-empty">
                <Search className="lib-empty-icon" size={44} />
                <h3>{hasActiveLibraryFilters && !librarySearchHasQuery ? t("library.empty.noFilteredGames") : t("library.empty.noGamesFound")}</h3>
                <p>
                  {librarySearchHasQuery
                    ? t("library.empty.noGamesMatch", { query: searchQuery })
                    : hasActiveLibraryFilters
                      ? t("library.empty.tryAdjustingFilters")
                      : t("library.empty.noGamesMatch", { query: searchQuery })}
                </p>
              </div>
            ) : (
              <>
                <div className="lib-grid">{libraryGridItems}</div>
                {pagedLibraryGames.length < visibleLibraryGames.length && (
                  <div className="lib-grid-more">
                    <span className="lib-grid-more-count">
                      {t("home.search.showing", {
                        shown: pagedLibraryGames.length,
                        total: visibleLibraryGames.length,
                      } as any)}
                    </span>
                    <button
                      type="button"
                      className="hd-search-loadmore"
                      onClick={() => setLibraryVisibleCount((count) => Math.min(
                        count + LIBRARY_PAGE_SIZE,
                        visibleLibraryGames.length,
                      ))}
                    >
                      <span>{t("home.search.loadMore") || "Load more"}</span>
                      <ChevronDown size={14} />
                    </button>
                  </div>
                )}
              </>
            )}
          </div>
        </div>

        <aside className="lib-detail" aria-label={selectedGame?.title ?? "Game details"}>
          {selectedGame ? (
            <>
              <div className="lib-detail-art">
                {selectedHero ? (
                  <img src={selectedHero} alt="" className="lib-detail-img" decoding="async" />
                ) : (
                  <span className="lib-detail-img lib-detail-img--placeholder" aria-hidden="true">
                    <Gamepad2 size={36} />
                  </span>
                )}
              </div>

              <div className="lib-detail-body">
                <p className="lib-detail-eyebrow">
                  <span className="lib-detail-eyebrow-dash" aria-hidden="true" />
                  {selectedStoreName || t("library.deck.eyebrow")}
                </p>
                <h3 className="lib-detail-title">{selectedGame.title}</h3>

                <dl className="lib-detail-cells">
                  {detailCells.map((cell) => (
                    <div className="lib-detail-cell" key={cell.id}>
                      <dt className="lib-detail-cell-label">{cell.label}</dt>
                      <dd className="lib-detail-cell-value" title={cell.value as string}>
                        {cell.icon && (
                          <span className="lib-detail-cell-icon" aria-hidden="true">
                            <cell.icon />
                          </span>
                        )}
                        {cell.value}
                      </dd>
                    </div>
                  ))}
                </dl>

                <div className="lib-detail-actions">
                  <button type="button" className="lib-launch" onClick={() => onPlayGame(selectedGame)}>
                    <span className="lib-launch-face">
                      <Play size={14} fill="currentColor" />
                      <span>{t("library.deck.launch")}</span>
                      <kbd>Enter</kbd>
                    </span>
                  </button>
                  {onNavigatePlaytime && selectedSeconds > 0 && (
                    <button type="button" className="lib-detail-link" onClick={onNavigatePlaytime}>
                      <Timer size={14} />
                      <span>{t("library.deck.viewLedger")}</span>
                    </button>
                  )}
                  <span className="lib-detail-live">
                    <Clock size={12} />
                    <span>{selectedLastPlayed}</span>
                  </span>
                </div>
              </div>
            </>
          ) : (
            <div className="lib-detail-empty">
              <Library size={28} />
              <p>{t("library.deck.emptyDetail")}</p>
            </div>
          )}
        </aside>
      </div>
    </div>
  );
});
