import { Search, LayoutGrid, ArrowUpDown, Filter, ChevronDown, ChevronRight, Gamepad2, Menu, Play } from "lucide-react";
import { memo, useEffect, useMemo, useRef, useState } from "react";
import type { JSX } from "react";
import { AnimatePresence, m } from "motion/react";
import { isOwnedLibraryStatus } from "@shared/gfn";
import type { CatalogFilterGroup, CatalogSortOption, GameInfo, GamePanelResult, GameVariant } from "@shared/gfn";
import { getStoreDisplayName, getStoreIconComponent } from "./GameCard";
import { GameCardListItem, useCatalogCardActionsRef } from "./GameCardListItem";
import { PosterCard } from "./PosterCard";
import type { PlaytimeData } from "../lib/gameCatalog";
import { appendImageType, appendUnique, gameMatchesActiveSession } from "../lib/controllerCatalogUi";
import { useTranslation } from "../i18n";
import { controllerButton, readControllerGamepadButtons } from "../utils/controllerGamepad";
import { pageTransition, panelSpring } from "./MotionProvider";
import { SelectDropdown } from "./ui/SelectDropdown";
import { MotionSpinner } from "./MotionSpinner";

const CONTROLLER_STORE_HERO_ROTATION_MS = 7000;
const CONTROLLER_MOVE_REPEAT_MS = 140;

const CONTROLLER_STORE_PROMINENT_IMAGE_KEYS = [
  "MARQUEE_HERO_IMAGE",
  "HERO_IMAGE",
  "TV_BANNER",
  "FEATURE_IMAGE",
  "KEY_ART",
  "KEY_IMAGE",
  "GAME_BOX_ART",
] as const;

const CONTROLLER_STORE_TILE_IMAGE_KEYS = [
  "TV_BANNER",
  "HERO_IMAGE",
  "KEY_IMAGE",
  "KEY_ART",
  "GAME_BOX_ART",
  "FEATURE_IMAGE",
] as const;

export interface HomePageProps {
  games: GameInfo[];
  searchQuery: string;
  onSearchChange: (query: string) => void;
  onPlayGame: (game: GameInfo) => void;
  isLoading: boolean;
  selectedGameId: string;
  onSelectGame: (id: string) => void;
  selectedVariantByGameId: Record<string, string>;
  onSelectGameVariant: (gameId: string, variantId: string) => void;
  filterGroups: CatalogFilterGroup[];
  selectedFilterIds: string[];
  onToggleFilter: (filterId: string) => void;
  sortOptions: CatalogSortOption[];
  selectedSortId: string;
  onSortChange: (sortId: string) => void;
  totalCount: number;
  supportedCount: number;
  controllerMode?: boolean;
  storePanels?: GamePanelResult[];
  storeHeroGames?: GameInfo[];
  activeSessionAppIds?: number[];
  onBuyGame?: (game: GameInfo, selectedVariantId?: string) => void;
  onMarkGameOwned?: (game: GameInfo, selectedVariantId?: string) => void;
  markOwnedInFlightByVariantId?: Record<string, boolean>;
  onPreviousControllerPage?: () => void;
  onNextControllerPage?: () => void;
  libraryGames?: GameInfo[];
  playtimeData?: PlaytimeData;
  favoriteGameIds?: string[];
  streamMetaLabel?: string;
  onNavigateLibrary?: () => void;
}

function getSteamHeaderUrl(game: GameInfo): string | undefined {
  const steamVariant = game.variants.find((variant) => /^\d+$/.test(variant.id) && variant.store.toUpperCase().includes("STEAM"));
  const appId = steamVariant?.id ?? (/^\d+$/.test(game.launchAppId ?? "") ? game.launchAppId : undefined);
  return appId ? `https://cdn.cloudflare.steamstatic.com/steam/apps/${appId}/header.jpg` : undefined;
}

function getControllerStoreImageCandidates(game: GameInfo, prominent: boolean): string[] {
  const candidates: string[] = [];
  const keys = prominent ? CONTROLLER_STORE_PROMINENT_IMAGE_KEYS : CONTROLLER_STORE_TILE_IMAGE_KEYS;
  for (const type of keys) appendImageType(candidates, game, type);
  appendUnique(candidates, game.heroImageUrl);
  appendUnique(candidates, game.imageUrl);
  for (const screenshot of game.screenshotUrls ?? []) {
    appendUnique(candidates, screenshot);
    if (!prominent) break;
  }
  appendUnique(candidates, game.screenshotUrl);
  appendUnique(candidates, getSteamHeaderUrl(game));
  return candidates;
}

function getControllerStoreLogoUrl(game: GameInfo): string | undefined {
  return game.imageUrlsByType?.GAME_LOGO?.[0]
    ?? game.imageUrlsByType?.LOGO?.[0]
    ?? game.imageUrlsByType?.TITLE_LOGO?.[0];
}

function getSelectedVariant(game: GameInfo, selectedVariantId?: string): GameVariant | undefined {
  return game.variants.find((variant) => variant.id === selectedVariantId)
    ?? game.variants[game.selectedVariantIndex]
    ?? game.variants[0];
}

function storeVariantIsOwned(variant: GameVariant | undefined): boolean {
  return Boolean(variant?.inLibrary || variant?.librarySelected || isOwnedLibraryStatus(variant?.libraryStatus));
}

function getVariantDisplayName(variant: GameVariant | undefined, fallback: string): string {
  return variant?.store ? getStoreDisplayName(variant.store) : fallback;
}

function getPurchaseUrl(game: GameInfo, selectedVariantId?: string): string | undefined {
  const selectedVariant = getSelectedVariant(game, selectedVariantId);
  if (selectedVariant?.storeUrl) return selectedVariant.storeUrl;
  return game.variants.find((variant) => !storeVariantIsOwned(variant) && variant.storeUrl)?.storeUrl
    ?? game.variants.find((variant) => variant.storeUrl)?.storeUrl;
}

function gameNeedsPurchase(game: GameInfo, selectedVariantId?: string): boolean {
  const selectedVariant = getSelectedVariant(game, selectedVariantId);
  return !storeVariantIsOwned(selectedVariant);
}

function getNextVariantId(game: GameInfo, selectedVariantId?: string): string | undefined {
  if (game.variants.length === 0) return undefined;
  const activeIndex = Math.max(0, game.variants.findIndex((variant) => variant.id === selectedVariantId));
  return game.variants[(activeIndex + 1) % game.variants.length]?.id;
}

function getPrimaryGenre(game: GameInfo): string {
  return game.genres?.[0] ?? game.playType ?? "Cloud Game";
}

function getPrimaryStoreName(game: GameInfo, selectedVariantId?: string): string {
  const store = getSelectedVariant(game, selectedVariantId)?.store ?? game.availableStores?.[0] ?? "Cloud";
  const upper = store.toUpperCase();
  if (upper.includes("STEAM")) return "Steam";
  if (upper.includes("BATTLE")) return "Battle.net";
  if (upper.includes("UBISOFT") || upper.includes("UPLAY")) return "Ubisoft";
  if (upper.includes("XBOX")) return "Xbox";
  if (upper.includes("EPIC")) return "Epic";
  if (upper.includes("EA")) return "EA";
  return getStoreDisplayName(store);
}

function ControllerStoreTile({
  game,
  selectedVariantId,
  focused,
  onFocus,
  onMarkOwned,
  onPlay,
  isMarkingOwned,
}: {
  game: GameInfo;
  selectedVariantId?: string;
  focused: boolean;
  onFocus: () => void;
  onMarkOwned: () => void;
  onPlay: () => void;
  isMarkingOwned: boolean;
}): JSX.Element {
  const { t } = useTranslation();
  const imageUrl = getControllerStoreImageCandidates(game, false)[0];
  const selectedVariant = getSelectedVariant(game, selectedVariantId);
  const storeName = getPrimaryStoreName(game, selectedVariantId);
  const StoreIcon = getStoreIconComponent(selectedVariant?.store ?? storeName);
  const needsPurchase = gameNeedsPurchase(game, selectedVariantId);

  return (
    <m.div
      role="button"
      tabIndex={0}
      className={`controller-store-tile${focused ? " focused" : ""}`}
      onClick={onFocus}
      onDoubleClick={() => {
        if (needsPurchase) {
          onMarkOwned();
          return;
        }
        onPlay();
      }}
      aria-label={game.title}
      animate={{ scale: focused ? 1.055 : 1 }}
      whileTap={{ scale: 0.98 }}
      transition={panelSpring}
    >
      <span className="controller-store-tile-art">
        {imageUrl ? <img src={imageUrl} alt="" loading="lazy" /> : <span className="controller-store-tile-placeholder">{game.title.slice(0, 1)}</span>}
      </span>
      <span className="controller-store-tile-gradient" />
      <span className="controller-store-tile-shine" />
      <span className="controller-store-tile-accent" />
      <span className="controller-store-tile-badge">
        <StoreIcon />
        <span>{storeName}</span>
      </span>
      <span className={`controller-store-tile-ownership${needsPurchase ? " is-not-owned" : " is-owned"}`}>
        {needsPurchase ? t("home.controller.notOwned") : t("home.controller.owned")}
      </span>
      {!needsPurchase && (
        <span className="controller-store-tile-variant">
          {t("home.controller.variant", { variant: getVariantDisplayName(selectedVariant, storeName) })}
        </span>
      )}
      <button
        type="button"
        className="controller-store-tile-action"
        onClick={(event) => {
          event.stopPropagation();
          onFocus();
          if (needsPurchase) {
            onMarkOwned();
            return;
          }
          onPlay();
        }}
        disabled={isMarkingOwned}
      >
        {needsPurchase ? (isMarkingOwned ? t("app.status.markingOwned") : t("app.actions.markAsOwned")) : t("app.actions.play")}
      </button>
    </m.div>
  );
}

export const HomePage = memo(function HomePage({
  games,
  searchQuery,
  onSearchChange,
  onPlayGame,
  isLoading,
  selectedGameId,
  onSelectGame,
  selectedVariantByGameId,
  onSelectGameVariant,
  filterGroups,
  selectedFilterIds,
  onToggleFilter,
  sortOptions,
  selectedSortId,
  onSortChange,
  totalCount,
  supportedCount,
  controllerMode = false,
  storePanels = [],
  storeHeroGames = [],
  activeSessionAppIds: _activeSessionAppIds = [],
  onBuyGame,
  onMarkGameOwned,
  markOwnedInFlightByVariantId = {},
  onPreviousControllerPage,
  onNextControllerPage,
  libraryGames = [],
  playtimeData = {},
  favoriteGameIds = [],
  streamMetaLabel,
  onNavigateLibrary,
}: HomePageProps): JSX.Element {
  const { t } = useTranslation();
  const catalogActionsRef = useCatalogCardActionsRef({
    onPlayGame,
    onSelectGame,
    onSelectGameVariant,
  });
  const [controllerHeroIndex, setControllerHeroIndex] = useState(0);
  const [focusedRowIndex, setFocusedRowIndex] = useState(0);
  const [focusedColumnIndex, setFocusedColumnIndex] = useState(0);
  const [controllerSearchOpen, setControllerSearchOpen] = useState(false);
  const rowRefs = useRef<Array<HTMLDivElement | null>>([]);
  const controllerSearchInputRef = useRef<HTMLInputElement | null>(null);
  const gamepadPreviousButtonsRef = useRef(0);
  const gamepadLastMoveAtRef = useRef(0);
  const gamepadFrameRef = useRef<number | null>(null);
  const controllerInputStateRef = useRef({
    focusTile: (_row: number, _column: number): void => {},
    launchFocusedTile: (): void => {},
    cycleFocusedVariant: (): boolean => false,
    focusedRowIndex: 0,
    focusedColumnIndex: 0,
  });

  const controllerSections = useMemo(
    () => storePanels.flatMap((panel) => panel.sections).filter((section) => section.games.length > 0),
    [storePanels],
  );
  const controllerHeroGames = useMemo(
    () => storeHeroGames.slice(0, 6),
    [storeHeroGames],
  );

  const focusTile = (rowIndex: number, columnIndex: number): void => {
    if (controllerSections.length === 0) return;
    const nextRowIndex = Math.max(0, Math.min(rowIndex, controllerSections.length - 1));
    const row = controllerSections[nextRowIndex];
    if (!row || row.games.length === 0) return;
    const nextColumnIndex = Math.max(0, Math.min(columnIndex, row.games.length - 1));
    const nextGame = row.games[nextColumnIndex];
    setFocusedRowIndex(nextRowIndex);
    setFocusedColumnIndex(nextColumnIndex);
    onSelectGame(nextGame.id);
    window.requestAnimationFrame(() => {
      const tile = rowRefs.current[nextRowIndex]?.querySelector<HTMLElement>(`[data-controller-store-column="${nextColumnIndex}"]`);
      tile?.scrollIntoView({ inline: "nearest", block: "nearest", behavior: "auto" });
      tile?.closest(".controller-store-section")?.scrollIntoView({ block: "nearest", inline: "nearest", behavior: "auto" });
    });
  };

  const launchGame = (game: GameInfo): void => {
    const selectedVariantId = selectedVariantByGameId[game.id];
    if (gameNeedsPurchase(game, selectedVariantId)) {
      (onMarkGameOwned ?? onBuyGame)?.(game, selectedVariantId);
      return;
    }
    onPlayGame(game);
  };

  const launchFocusedTile = (): void => {
    const game = controllerSections[focusedRowIndex]?.games[focusedColumnIndex];
    if (game) launchGame(game);
  };

  const cycleFocusedVariant = (): boolean => {
    const game = controllerSections[focusedRowIndex]?.games[focusedColumnIndex];
    if (!game || game.variants.length <= 1) return false;
    const nextVariantId = getNextVariantId(game, selectedVariantByGameId[game.id]);
    if (!nextVariantId) return false;
    onSelectGameVariant(game.id, nextVariantId);
    return true;
  };

  useEffect(() => {
    controllerInputStateRef.current = {
      focusTile,
      launchFocusedTile,
      cycleFocusedVariant,
      focusedRowIndex,
      focusedColumnIndex,
    };
  }, [cycleFocusedVariant, focusedColumnIndex, focusedRowIndex, focusTile, launchFocusedTile]);

  useEffect(() => {
    if (!controllerMode || !controllerSearchOpen) return;
    controllerSearchInputRef.current?.focus();
  }, [controllerMode, controllerSearchOpen]);

  useEffect(() => {
    if (!controllerMode) return;
    setControllerHeroIndex(0);
  }, [controllerHeroGames, controllerMode]);

  useEffect(() => {
    if (!controllerMode || controllerHeroGames.length <= 1) return;
    const interval = window.setInterval(() => {
      setControllerHeroIndex((index) => (index + 1) % controllerHeroGames.length);
    }, CONTROLLER_STORE_HERO_ROTATION_MS);
    return () => window.clearInterval(interval);
  }, [controllerHeroGames.length, controllerMode]);

  useEffect(() => {
    if (!controllerMode || controllerSections.length === 0) return;
    const currentRow = controllerSections[focusedRowIndex];
    if (currentRow?.games.some((game) => game.id === selectedGameId)) return;
    focusTile(0, 0);
  }, [controllerMode, controllerSections, focusedRowIndex, selectedGameId]);

  useEffect(() => {
    if (!controllerMode) return;
    const handleKeyDown = (event: KeyboardEvent) => {
      if (controllerSearchOpen) {
        if (event.key === "Escape") {
          event.preventDefault();
          setControllerSearchOpen(false);
        }
        return;
      }
      if (event.key === "ArrowLeft") {
        event.preventDefault();
        focusTile(focusedRowIndex, focusedColumnIndex - 1);
      } else if (event.key === "ArrowRight") {
        event.preventDefault();
        focusTile(focusedRowIndex, focusedColumnIndex + 1);
      } else if (event.key === "ArrowUp") {
        event.preventDefault();
        focusTile(focusedRowIndex - 1, focusedColumnIndex);
      } else if (event.key === "ArrowDown") {
        event.preventDefault();
        focusTile(focusedRowIndex + 1, focusedColumnIndex);
      } else if (event.key.toLowerCase() === "x") {
        event.preventDefault();
        setControllerSearchOpen(true);
      } else if (event.key === "Escape") {
        event.preventDefault();
        onPreviousControllerPage?.();
      } else if (event.key.toLowerCase() === "b") {
        event.preventDefault();
        onPreviousControllerPage?.();
      } else if (event.key === "[") {
        event.preventDefault();
        onPreviousControllerPage?.();
      } else if (event.key === "]") {
        event.preventDefault();
        onNextControllerPage?.();
      } else if (event.key.toLowerCase() === "m" || event.key.toLowerCase() === "y") {
        event.preventDefault();
        cycleFocusedVariant();
      } else if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        launchFocusedTile();
      }
    };
    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, [controllerMode, controllerSearchOpen, cycleFocusedVariant, focusedColumnIndex, focusedRowIndex, focusTile, launchFocusedTile, onNextControllerPage, onPreviousControllerPage]);

  useEffect(() => {
    if (!controllerMode) return;
    const readButtons = (): number => {
      const pad = navigator.getGamepads?.().find((gamepad): gamepad is Gamepad => Boolean(gamepad));
      return readControllerGamepadButtons(pad);
    };

    const handleGamepadFrame = () => {
      const buttons = readButtons();
      let pressed = buttons & ~gamepadPreviousButtonsRef.current;
      const moveMask = controllerButton.up | controllerButton.down | controllerButton.left | controllerButton.right;
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
        focusTile: focusControllerTile,
        launchFocusedTile: launchControllerTile,
        cycleFocusedVariant: cycleControllerVariant,
        focusedRowIndex: rowIndex,
        focusedColumnIndex: columnIndex,
      } = controllerInputStateRef.current;

      if (controllerSearchOpen) {
        if (pressed & controllerButton.east) setControllerSearchOpen(false);
        gamepadPreviousButtonsRef.current = buttons;
        gamepadFrameRef.current = window.requestAnimationFrame(handleGamepadFrame);
        return;
      }

      if (pressed & controllerButton.south) launchControllerTile();
      if (pressed & controllerButton.east) onPreviousControllerPage?.();
      if (pressed & controllerButton.west) setControllerSearchOpen(true);
      if (pressed & controllerButton.leftShoulder) onPreviousControllerPage?.();
      if (pressed & controllerButton.rightShoulder) onNextControllerPage?.();
      if (pressed & controllerButton.menu) cycleControllerVariant();
      if (pressed & controllerButton.up) focusControllerTile(rowIndex - 1, columnIndex);
      if (pressed & controllerButton.down) focusControllerTile(rowIndex + 1, columnIndex);
      if (pressed & controllerButton.left) focusControllerTile(rowIndex, columnIndex - 1);
      if (pressed & controllerButton.right) focusControllerTile(rowIndex, columnIndex + 1);
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

  const gameGridItems = useMemo(
    () => games.map((game) => (
      <GameCardListItem
        key={game.id}
        game={game}
        isSelected={game.id === selectedGameId}
        selectedVariantId={selectedVariantByGameId[game.id]}
        actionsRef={catalogActionsRef}
      />
    )),
    [catalogActionsRef, games, selectedGameId, selectedVariantByGameId],
  );

  if (controllerMode) {
    const showInitialLoading = isLoading && controllerSections.length === 0;
    const heroGame = controllerHeroGames[controllerHeroIndex];
    const heroImageUrl = heroGame ? getControllerStoreImageCandidates(heroGame, true)[0] : undefined;
    const heroLogoUrl = heroGame ? getControllerStoreLogoUrl(heroGame) : undefined;
    const heroSelectedVariantId = heroGame ? selectedVariantByGameId[heroGame.id] : undefined;
    const heroSelectedVariant = heroGame ? getSelectedVariant(heroGame, heroSelectedVariantId) : undefined;
    const heroNeedsOwnership = heroGame ? gameNeedsPurchase(heroGame, heroSelectedVariantId) : false;
    const heroMarkingOwned = Boolean(heroNeedsOwnership && heroSelectedVariant?.id && markOwnedInFlightByVariantId[heroSelectedVariant.id]);
    const heroDotCount = Math.min(Math.max(controllerHeroGames.length, 1), 6);
    const activeHeroDotIndex = controllerHeroGames.length > 0 ? Math.min(controllerHeroIndex % heroDotCount, heroDotCount - 1) : 0;

    return (
      <div className="home-page controller-store-page">
        {showInitialLoading ? (
          <div className="home-empty-state controller-store-empty">
            <MotionSpinner className="home-spinner" size={54} label={t("common.loading")} />
            <p>{t("home.empty.loadingGames")}</p>
          </div>
        ) : controllerSections.length === 0 ? (
          <div className="home-empty-state controller-store-empty">
            <Gamepad2 className="home-empty-icon" size={64} />
            <h3>{t("home.controller.emptyTitle")}</h3>
            <p>{t("home.controller.emptyBody")}</p>
          </div>
        ) : (
          <>
            {heroGame && (
              <section className="controller-hero controller-store-hero" aria-label={heroGame.title}>
                <AnimatePresence initial={false} mode="popLayout">
                  {heroImageUrl ? (
                    <m.img
                      key={heroImageUrl}
                      src={heroImageUrl}
                      alt=""
                      className="controller-hero-image"
                      initial={{ opacity: 0, scale: 1.035 }}
                      animate={{ opacity: 1, scale: 1 }}
                      exit={{ opacity: 0, scale: 1.015 }}
                      transition={pageTransition}
                    />
                  ) : (
                    <m.div
                      key="controller-store-hero-placeholder"
                      className="controller-hero-placeholder"
                      initial={{ opacity: 0 }}
                      animate={{ opacity: 1 }}
                      exit={{ opacity: 0 }}
                      transition={pageTransition}
                    />
                  )}
                </AnimatePresence>
                <div className="controller-hero-scrim" />
                <AnimatePresence initial={false} mode="wait">
                  <m.div
                    key={heroGame.id}
                    className="controller-hero-content"
                    initial={{ opacity: 0, y: 16 }}
                    animate={{ opacity: 1, y: 0 }}
                    exit={{ opacity: 0, y: -8 }}
                    transition={pageTransition}
                  >
                    {heroLogoUrl ? <img src={heroLogoUrl} alt={heroGame.title} className="controller-hero-logo" /> : <h1>{heroGame.title}</h1>}
                    <p className="controller-store-hero-meta">{getPrimaryStoreName(heroGame, heroSelectedVariantId)} / {getPrimaryGenre(heroGame)}</p>
                    <div className="controller-hero-actions">
                      <button
                        type="button"
                        className="controller-primary-action"
                        onClick={() => {
                          if (heroNeedsOwnership) {
                            (onMarkGameOwned ?? onBuyGame)?.(heroGame, heroSelectedVariantId);
                            return;
                          }
                          onPlayGame(heroGame);
                        }}
                        disabled={heroMarkingOwned}
                      >
                        {heroNeedsOwnership
                          ? (heroMarkingOwned ? t("app.status.markingOwned") : t("app.actions.markAsOwned"))
                          : t("app.actions.play")}
                      </button>
                      <span className="controller-store-hero-pill">{getPrimaryStoreName(heroGame, heroSelectedVariantId)}</span>
                    </div>
                  </m.div>
                </AnimatePresence>
              </section>
            )}

            {heroGame && (
              <div className="controller-hero-dots" aria-hidden="true">
                {Array.from({ length: heroDotCount }).map((_, index) => (
                  <span key={index} className={index === activeHeroDotIndex ? "active" : ""} />
                ))}
              </div>
            )}

            <div className="controller-store-sections">
              {controllerSections.map((section, rowIndex) => (
                <section key={`${section.id}-${rowIndex}`} className="controller-store-section">
                  <div className="controller-store-section-heading">
                    <span>{String(rowIndex + 1).padStart(2, "0")}</span>
                    <h2>{section.title || t("home.controller.featured")}</h2>
                    <p>{t("library.gameCount", { count: section.games.length })}</p>
                  </div>
                  <div
                    className="controller-store-row"
                    ref={(element) => { rowRefs.current[rowIndex] = element; }}
                    data-controller-store-row={rowIndex}
                  >
                    {section.games.slice(0, 18).map((game, columnIndex) => {
                      const focused = rowIndex === focusedRowIndex && columnIndex === focusedColumnIndex;
                      const selectedVariantId = selectedVariantByGameId[game.id];
                      const selectedVariant = getSelectedVariant(game, selectedVariantId);
                      return (
                        <div key={game.id} className="controller-store-card" data-controller-store-column={columnIndex}>
                          <ControllerStoreTile
                            game={game}
                            selectedVariantId={selectedVariantId}
                            isMarkingOwned={Boolean(selectedVariant?.id && markOwnedInFlightByVariantId[selectedVariant.id])}
                            focused={focused}
                            onFocus={() => {
                              focusTile(rowIndex, columnIndex);
                              if (game.variants.length > 0) onSelectGameVariant(game.id, selectedVariantId ?? game.variants[game.selectedVariantIndex]?.id ?? game.variants[0].id);
                            }}
                            onMarkOwned={() => (onMarkGameOwned ?? onBuyGame)?.(game, selectedVariantId)}
                            onPlay={() => onPlayGame(game)}
                          />
                        </div>
                      );
                    })}
                  </div>
                </section>
              ))}
            </div>

            <div className="controller-bottom-hints" aria-hidden="true">
              <div className="controller-hint"><span className="controller-button controller-button--a">A</span><span>{t("app.actions.select")}</span></div>
              <div className="controller-hint"><span className="controller-button controller-button--b">B</span><span>{t("app.actions.back")}</span></div>
              <div className="controller-hint"><span className="controller-button controller-button--x">X</span><span>{t("app.actions.search")}</span></div>
              <div className="controller-hint controller-hint--more"><span className="controller-menu-button"><Menu size={22} /></span><span>{t("library.moreOptions")}</span></div>
            </div>

            <AnimatePresence initial={false}>
              {controllerSearchOpen && (
                <m.div
                  className="controller-search-overlay"
                  role="dialog"
                  aria-modal="true"
                  aria-label={t("app.actions.search")}
                  initial={{ opacity: 0 }}
                  animate={{ opacity: 1 }}
                  exit={{ opacity: 0 }}
                  transition={pageTransition}
                >
                  <m.div
                    className="controller-search-panel"
                    initial={{ opacity: 0, y: 16, scale: 0.985 }}
                    animate={{ opacity: 1, y: 0, scale: 1 }}
                    exit={{ opacity: 0, y: 10, scale: 0.99 }}
                    transition={panelSpring}
                  >
                  <span className="controller-search-eyebrow">{t("app.actions.search")}</span>
                  <input
                    ref={controllerSearchInputRef}
                    type="text"
                    value={searchQuery}
                    onChange={(event) => onSearchChange(event.target.value)}
                    placeholder={t("home.searchPlaceholder")}
                    className="controller-search-input"
                  />
                  <p>{t("app.actions.back")}</p>
                  </m.div>
                </m.div>
              )}
            </AnimatePresence>
          </>
        )}
      </div>
    );
  }

  const hasGames = games.length > 0;
  const showInitialLoading = isLoading && !hasGames;
  const isSearching = searchQuery.trim().length > 0;

  // Build rich home shelves from the owned library + playtime signals.
  const librarySource = libraryGames.length > 0 ? libraryGames : games;
  const playtimeMs = (game: GameInfo): number => {
    const raw = playtimeData[game.id]?.lastPlayedAt ?? game.lastPlayed;
    const ms = raw ? Date.parse(raw) : NaN;
    return Number.isFinite(ms) ? ms : 0;
  };

  const recentlyPlayed = useMemo(
    () => [...librarySource]
      .filter((game) => playtimeMs(game) > 0)
      .sort((a, b) => playtimeMs(b) - playtimeMs(a)),
    [librarySource, playtimeData],
  );

  const heroGame = recentlyPlayed[0] ?? librarySource[0] ?? games[0];

  const jumpBackIn = useMemo(
    () => recentlyPlayed.filter((game) => game.id !== heroGame?.id).slice(0, 12),
    [recentlyPlayed, heroGame],
  );

  const favourites = useMemo(() => {
    const favSet = new Set(favoriteGameIds);
    const favs = librarySource.filter((game) => favSet.has(game.id));
    if (favs.length > 0) return favs.slice(0, 12);
    // Fallback: surface a stable pseudo-favourites shelf so the row is never empty.
    return [...librarySource].slice(0, 12);
  }, [librarySource, favoriteGameIds]);

  const newInLibrary = useMemo(
    () => [...librarySource].reverse().slice(0, 12),
    [librarySource],
  );

  const heroPlaytimeSeconds = heroGame ? playtimeData[heroGame.id]?.totalSeconds ?? 0 : 0;
  const heroLastPlayedMs = heroGame ? playtimeMs(heroGame) : 0;

  const formatRelative = (ms: number): string => {
    if (!ms) return "";
    const diff = Date.now() - ms;
    const mins = Math.floor(diff / 60000);
    const hours = Math.floor(diff / 3600000);
    const days = Math.floor(diff / 86400000);
    if (mins < 1) return "just now";
    if (mins < 60) return `${mins} min ago`;
    if (hours < 24) return `${hours} ${hours === 1 ? "hour" : "hours"} ago`;
    if (days < 7) return `${days} ${days === 1 ? "day" : "days"} ago`;
    return new Date(ms).toLocaleDateString();
  };
  const formatPlayed = (seconds: number): string => {
    if (!seconds) return "";
    const hours = seconds / 3600;
    if (hours < 1) return `${Math.max(1, Math.round(seconds / 60))} m played`;
    return `${hours >= 10 ? Math.round(hours) : Math.round(hours * 10) / 10} h played`;
  };

  const heroBg = heroGame
    ? (heroGame.heroImageUrl
        ?? heroGame.imageUrlsByType?.HERO_IMAGE?.[0]
        ?? heroGame.imageUrlsByType?.KEY_ART?.[0]
        ?? heroGame.screenshotUrls?.[0]
        ?? heroGame.imageUrl)
    : undefined;

  const heroMetaParts = [formatRelative(heroLastPlayedMs), formatPlayed(heroPlaytimeSeconds)].filter(Boolean);

  const heroVariant = heroGame ? (heroGame.variants[heroGame.selectedVariantIndex] ?? heroGame.variants[0]) : undefined;
  const heroStoreRaw = heroVariant?.store ?? heroGame?.availableStores?.[0];
  const HeroStoreIcon = heroStoreRaw ? getStoreIconComponent(heroStoreRaw) : null;
  const heroStoreName = heroStoreRaw ? getStoreDisplayName(heroStoreRaw) : "";

  const renderRow = (title: string, rowGames: GameInfo[], seeAllCount?: number): JSX.Element | null => {
    if (rowGames.length === 0) return null;
    return (
      <section className="home-shelf" key={title}>
        <div className="home-shelf-head">
          <h2 className="home-shelf-title">{title}</h2>
          {onNavigateLibrary && (
            <button type="button" className="home-shelf-seeall" onClick={onNavigateLibrary}>
              {seeAllCount ? `See all ${seeAllCount}` : "See all"}
              <ChevronRight size={14} />
            </button>
          )}
        </div>
        <div className="home-shelf-row">
          {rowGames.map((game) => (
            <PosterCard
              key={game.id}
              game={game}
              isSelected={game.id === selectedGameId}
              onSelect={() => onSelectGame(game.id)}
              onPlay={() => onPlayGame(game)}
            />
          ))}
        </div>
      </section>
    );
  };

  return (
    <div className="home-page home-page--v2">
      <div className="home-scroll">
        {showInitialLoading ? (
          <div className="home-empty-state">
            <MotionSpinner className="home-spinner" size={36} label={t("common.loading")} />
            <p>{t("home.empty.loadingGames")}</p>
          </div>
        ) : isSearching ? (
          hasGames ? (
            <div className="game-grid game-grid--search">{gameGridItems}</div>
          ) : (
            <div className="home-empty-state">
              <Search size={44} className="home-empty-icon" />
              <h3>{t("home.empty.noGamesFound")}</h3>
              <p>{t("home.empty.tryAdjustingSearch")}</p>
            </div>
          )
        ) : (
          <>
            {heroGame && (
              <section className="home-hero" aria-label={heroGame.title}>
                {heroBg ? (
                  <img src={heroBg} alt="" className="home-hero-bg" />
                ) : (
                  <div className="home-hero-bg home-hero-bg--placeholder" />
                )}
                <div className="home-hero-scrim" />
                <div className="home-hero-content">
                  <span className="home-hero-eyebrow">Continue playing</span>
                  <h1 className="home-hero-title">{heroGame.title}</h1>
                  {heroMetaParts.length > 0 && (
                    <p className="home-hero-meta">{heroMetaParts.join(" · ")}</p>
                  )}
                  <div className="home-hero-actions">
                    <button type="button" className="home-hero-play" onClick={() => onPlayGame(heroGame)}>
                      <Play size={16} fill="currentColor" />
                      <span>Start</span>
                      <kbd>Enter</kbd>
                    </button>
                    {heroStoreRaw && HeroStoreIcon && (
                      <span className="home-hero-store" title={heroStoreName}>
                        <span className="home-hero-store-icon"><HeroStoreIcon /></span>
                        <span>{heroStoreName}</span>
                      </span>
                    )}
                    {streamMetaLabel && (
                      <span className="home-hero-stats">
                        <span className="home-hero-stats-dot" />
                        {streamMetaLabel}
                      </span>
                    )}
                  </div>
                </div>
              </section>
            )}

            {renderRow("Jump back in", jumpBackIn, jumpBackIn.length)}
            {renderRow("Favourites", favourites)}
            {renderRow("New in your library", newInLibrary)}

            {!heroGame && (
              <div className="home-empty-state">
                <LayoutGrid size={44} className="home-empty-icon" />
                <h3>{t("home.empty.noGamesFound")}</h3>
                <p>{t("home.empty.checkBackLater")}</p>
              </div>
            )}
          </>
        )}
      </div>
    </div>
  );
});
