import { X, Play, Heart, ShoppingCart, Check, AlertCircle, Monitor } from "lucide-react";
import { memo, useEffect, useMemo, useState, type JSX } from "react";
import { m } from "motion/react";
import type { GameInfo } from "@shared/gfn";
import { isGameInLibrary, isOwnedVariant, normalizeGameStore } from "@shared/gfn";
import { getStoreDisplayName, getStoreIconComponent } from "./GameCard";
import { getStoreOptions, type StoreOption } from "../lib/gameCardStores";
import { useTranslation } from "../i18n";
import { formatCatalogLastPlayed } from "../utils/lastPlayedFormat";
import { formatPlaytimeDuration } from "../utils/playtimeFormat";
import type { PlaytimeData } from "../lib/gameCatalog";

export interface GameDetailsPageProps {
  game: GameInfo;
  playtimeData?: PlaytimeData;
  selectedVariantId?: string;
  onSelectVariant: (variantId: string) => void;
  onMarkOwned: (variantId: string) => void;
  onBuy: (variantId: string) => void;
  onPlay: () => void;
  onClose: () => void;
  onToggleFavorite?: () => void;
  isFavorite?: boolean;
  isMarkingOwned?: boolean;
}

function getPrimaryGenre(game: GameInfo): string {
  return game.genres?.[0] ?? game.playType ?? "";
}

function isSubscriptionStore(storeKey: string): boolean {
  const key = storeKey.toUpperCase();
  return (
    key.includes("PLUS") ||
    key.includes("GAME_PASS") ||
    key.includes("PASS") ||
    key.includes("SUBSCRIPTION") ||
    key.includes("CLASSICS") ||
    key.includes("PREMIUM") ||
    key.includes("EA_PLAY") ||
    key.includes("UBISOFT_PLUS")
  );
}

function getPosterUrl(game: GameInfo): string | undefined {
  return (
    game.imageUrlsByType?.GAME_BOX_ART?.[0] ??
    game.imageUrlsByType?.KEY_ART?.[0] ??
    game.imageUrl ??
    game.imageUrlsByType?.KEY_IMAGE?.[0] ??
    game.heroImageUrl
  );
}

function getHeroUrl(game: GameInfo): string | undefined {
  return (
    game.heroImageUrl ??
    game.imageUrlsByType?.HERO_IMAGE?.[0] ??
    game.imageUrlsByType?.MARQUEE_HERO_IMAGE?.[0] ??
    game.imageUrlsByType?.KEY_ART?.[0] ??
    game.screenshotUrls?.[0] ??
    game.imageUrl
  );
}

function getScreenshots(game: GameInfo): string[] {
  const urls: string[] = [];
  const push = (u?: string | string[]): void => {
    if (!u) return;
    if (Array.isArray(u)) {
      for (const v of u) if (v) urls.push(v);
    } else {
      urls.push(u);
    }
  };
  // Prefer hero / screenshots first
  push(game.heroImageUrl);
  push(game.imageUrlsByType?.HERO_IMAGE);
  push(game.imageUrlsByType?.MARQUEE_HERO_IMAGE);
  push(game.screenshotUrls);
  push(game.imageUrlsByType?.SCREENSHOTS);
  push(game.imageUrlsByType?.KEY_ART);
  push(game.imageUrlsByType?.FEATURE_IMAGE);
  push(game.imageUrlsByType?.KEY_IMAGE);
  push(game.imageUrlsByType?.GAME_BOX_ART);
  push(game.imageUrl);
  push(game.screenshotUrl);
  // Also include any other image types present
  if (game.imageUrlsByType) {
    for (const key of Object.keys(game.imageUrlsByType)) {
      const arr = (game.imageUrlsByType as Record<string, string[]>)[key];
      if (Array.isArray(arr)) push(arr);
    }
  }
  // dedupe preserve order
  const seen = new Set<string>();
  const out: string[] = [];
  for (const u of urls) {
    if (!u || seen.has(u)) continue;
    seen.add(u);
    out.push(u);
  }
  return out.slice(0, 24);
}

export const GameDetailsPage = memo(function GameDetailsPage({
  game,
  playtimeData,
  selectedVariantId,
  onSelectVariant,
  onMarkOwned,
  onBuy,
  onPlay,
  onClose,
  onToggleFavorite,
  isFavorite,
  isMarkingOwned,
}: GameDetailsPageProps): JSX.Element {
  const { t } = useTranslation();
  useEffect(() => {
    const handleKey = (e: KeyboardEvent): void => {
      if (e.key === "Escape") onClose();
    };
    window.addEventListener("keydown", handleKey);
    return () => window.removeEventListener("keydown", handleKey);
  }, [onClose]);
  const isOwned = isGameInLibrary(game);
  const posterUrl = getPosterUrl(game);
  const heroUrl = getHeroUrl(game);
  const screenshots = getScreenshots(game);
  const [selectedArtIndex, setSelectedArtIndex] = useState(0);
  // keep index in bounds when game changes
  useEffect(() => {
    setSelectedArtIndex(0);
  }, [game.id]);
  const mainArtUrl = screenshots[selectedArtIndex] ?? heroUrl ?? posterUrl;
  const play = playtimeData?.[game.id];
  const playtime = play?.totalSeconds ?? 0;
  const lastPlayedIso = play?.lastPlayedAt ?? game.lastPlayed;
  const lastPlayed = formatCatalogLastPlayed(t, lastPlayedIso ?? undefined);
  const genre = getPrimaryGenre(game);
  const storeOptions = useMemo(() => getStoreOptions(game, selectedVariantId), [game, selectedVariantId]);

  const gameStores = storeOptions.filter((opt) => !isSubscriptionStore(opt.storeKey));
  const subStores = storeOptions.filter((opt) => isSubscriptionStore(opt.storeKey));

  const selectedOption = storeOptions.find((opt) => opt.isActive) ?? storeOptions[0];
  const selectedStoreName = selectedOption ? getStoreDisplayName(selectedOption.store) : "";

  const contentRating = game.contentRatings?.[0] ?? "";
  const publisher = game.publisherName ?? "";
  const developer = game.developerName ?? "";
  const description = game.longDescription ?? game.description ?? "";

  return (
    <div className="gdp-root">
      {heroUrl && (
        <div className="gdp-ambient" aria-hidden="true">
          <img src={heroUrl} alt="" className="gdp-ambient-img" />
          <span className="gdp-ambient-bloom" />
          <span className="gdp-ambient-scrim" />
          <span className="gdp-ambient-scan" />
        </div>
      )}

      <div className="gdp-shell">
        <div className="gdp-topbar">
          <button type="button" className="gdp-close" onClick={onClose} aria-label={t("common.close")}>
            <X size={20} />
          </button>
        </div>

        <div className="gdp-layout">
          {/* Left: poster */}
          <div className="gdp-poster-col">
            <div className="gdp-poster-frame">
              {posterUrl ? (
                <img src={posterUrl} alt="" className="gdp-poster-img" />
              ) : (
                <span className="gdp-poster-placeholder">
                  <Monitor size={32} />
                  <span>{game.title}</span>
                </span>
              )}
              <span className="gdp-poster-brackets" aria-hidden="true">
                <span className="gdp-bracket gdp-bracket--tl" />
                <span className="gdp-bracket gdp-bracket--tr" />
                <span className="gdp-bracket gdp-bracket--bl" />
                <span className="gdp-bracket gdp-bracket--br" />
              </span>
            </div>

            <div className="gdp-poster-meta">
              <p className="gdp-poster-title">{game.title}</p>
              <p className="gdp-poster-subtitle">{t("gameDetails.pcDigitalVersion")}</p>
            </div>
          </div>

          {/* Middle: details */}
          <div className="gdp-details-col">
            <div className="gdp-title-row">
              <div className="gdp-title-block">
                <h1 className="gdp-title">{game.title}</h1>
                <p className="gdp-subtitle">{t("gameDetails.pcDigitalVersion")}</p>
              </div>
              {onToggleFavorite && (
                <button
                  type="button"
                  className={`gdp-fav${isFavorite ? " active" : ""}`}
                  onClick={onToggleFavorite}
                  aria-label={t("library.favoriteToggle")}
                  aria-pressed={isFavorite}
                >
                  <Heart size={18} fill={isFavorite ? "currentColor" : "none"} />
                </button>
              )}
            </div>

            <div className="gdp-meta-line">
              {contentRating && <span className="gdp-meta-chip">{contentRating}</span>}
              <span className="gdp-meta-dot" aria-hidden="true" />
              <span className="gdp-meta-item">{genre || "Cloud Game"}</span>
              {playtime > 0 && (
                <>
                  <span className="gdp-meta-dot" aria-hidden="true" />
                  <span className="gdp-meta-item">{formatPlaytimeDuration(t, playtime)} played</span>
                </>
              )}
              {lastPlayed && playtime > 0 && (
                <>
                  <span className="gdp-meta-dot" aria-hidden="true" />
                  <span className="gdp-meta-item">{lastPlayed}</span>
                </>
              )}
            </div>

            {!isOwned ? (
              <div className="gdp-store-picker">
                <h2 className="gdp-picker-title">{t("gameDetails.chooseStore")}</h2>
                <p className="gdp-picker-subtitle">{t("gameDetails.chooseStoreHint")}</p>

                {gameStores.length > 0 && (
                  <div className="gdp-store-group">
                    <h3 className="gdp-group-label">{t("gameDetails.gameStores")}</h3>
                    <div className="gdp-store-list">
                      {gameStores.map((opt) => {
                        const Icon = getStoreIconComponent(opt.store);
                        const isActive = opt.variantId === selectedOption?.variantId;
                        return (
                          <button
                            key={opt.storeKey}
                            type="button"
                            className={`gdp-store-row${isActive ? " active" : ""}${opt.isOwned ? " owned" : ""}`}
                            onClick={() => onSelectVariant(opt.variantId)}
                          >
                            <span className="gdp-store-left">
                              <span className="gdp-store-icon">
                                <Icon />
                              </span>
                              <span className="gdp-store-name">{getStoreDisplayName(opt.store)}</span>
                            </span>
                            <span className="gdp-store-right">
                              <span className={`gdp-store-status${opt.isOwned ? " owned" : " missing"}`}>
                                {opt.isOwned ? (
                                  <>
                                    <span>{t("gameDetails.owned")}</span>
                                    <Check size={12} />
                                  </>
                                ) : (
                                  <span>{t("gameDetails.gameNotFound")}</span>
                                )}
                              </span>
                            </span>
                          </button>
                        );
                      })}
                    </div>
                  </div>
                )}

                {subStores.length > 0 && (
                  <div className="gdp-store-group">
                    <h3 className="gdp-group-label">{t("gameDetails.subscriptions")}</h3>
                    <div className="gdp-store-list">
                      {subStores.map((opt) => {
                        const Icon = getStoreIconComponent(opt.store);
                        const isActive = opt.variantId === selectedOption?.variantId;
                        return (
                          <button
                            key={opt.storeKey}
                            type="button"
                            className={`gdp-store-row${isActive ? " active" : ""}${opt.isOwned ? " owned" : ""}`}
                            onClick={() => onSelectVariant(opt.variantId)}
                          >
                            <span className="gdp-store-left">
                              <span className="gdp-store-icon">
                                <Icon />
                              </span>
                              <span className="gdp-store-name">{getStoreDisplayName(opt.store)}</span>
                            </span>
                            <span className="gdp-store-right">
                              <span className={`gdp-store-status${opt.isOwned ? " owned" : " missing"}`}>
                                {opt.isOwned ? (
                                  <>
                                    <span>{t("gameDetails.subscribed")}</span>
                                    <Check size={12} />
                                  </>
                                ) : (
                                  <span>{t("gameDetails.notSubscribed")}</span>
                                )}
                              </span>
                            </span>
                          </button>
                        );
                      })}
                    </div>
                  </div>
                )}

                <div className="gdp-actions">
                  <button
                    type="button"
                    className="gdp-btn gdp-btn--primary"
                    onClick={() => selectedOption && onMarkOwned(selectedOption.variantId)}
                    disabled={!selectedOption || isMarkingOwned}
                  >
                    <span className="gdp-btn-face">
                      {isMarkingOwned ? t("common.loading") : t("gameDetails.markAsOwned")}
                    </span>
                  </button>
                  <button type="button" className="gdp-btn gdp-btn--secondary" onClick={() => selectedOption && onBuy(selectedOption.variantId)} disabled={!selectedOption}>
                    <span className="gdp-btn-face">
                      <ShoppingCart size={14} />
                      <span>{t("gameDetails.buy")}</span>
                    </span>
                  </button>
                </div>
              </div>
            ) : (
              <>
                {description && (
                  <div className="gdp-desc-block">
                    <p className="gdp-desc">{description}</p>
                  </div>
                )}

                <div className="gdp-info-grid">
                  {contentRating && (
                    <div className="gdp-info-row">
                      <span className="gdp-info-label">{t("gameDetails.rating")}</span>
                      <span className="gdp-info-value">{contentRating}</span>
                    </div>
                  )}
                  {publisher && (
                    <div className="gdp-info-row">
                      <span className="gdp-info-label">{t("gameDetails.publisher")}</span>
                      <span className="gdp-info-value">{publisher}</span>
                    </div>
                  )}
                  {developer && (
                    <div className="gdp-info-row">
                      <span className="gdp-info-label">{t("gameDetails.developer")}</span>
                      <span className="gdp-info-value">{developer}</span>
                    </div>
                  )}
                  {playtime > 0 && (
                    <div className="gdp-info-row">
                      <span className="gdp-info-label">{t("library.deck.cellPlaytime")}</span>
                      <span className="gdp-info-value">{formatPlaytimeDuration(t, playtime)}</span>
                    </div>
                  )}
                </div>

                <div className="gdp-actions">
                  <button type="button" className="gdp-btn gdp-btn--primary" onClick={onPlay}>
                    <span className="gdp-btn-face">
                      <Play size={14} fill="currentColor" />
                      <span>{t("app.actions.play")}</span>
                      <kbd>Enter</kbd>
                    </span>
                  </button>
                  {selectedOption && (
                    <button type="button" className="gdp-btn gdp-btn--secondary" onClick={() => onBuy(selectedOption.variantId)}>
                      <span className="gdp-btn-face">
                        <ShoppingCart size={14} />
                        <span>{t("gameDetails.buy")}</span>
                      </span>
                    </button>
                  )}
                </div>
              </>
            )}
          </div>

          {/* Right: screenshots / art - now full gallery */}
          <div className="gdp-art-col">
            <div className="gdp-art-frame">
              {mainArtUrl ? (
                <img src={mainArtUrl} alt="" className="gdp-art-img" />
              ) : (
                <span className="gdp-art-placeholder">
                  <Monitor size={28} />
                </span>
              )}
              <span className="gdp-art-brackets" aria-hidden="true">
                <span className="gdp-bracket gdp-bracket--tl" />
                <span className="gdp-bracket gdp-bracket--tr" />
                <span className="gdp-bracket gdp-bracket--bl" />
                <span className="gdp-bracket gdp-bracket--br" />
              </span>
              <span className="gdp-art-ticks" aria-hidden="true" />
            </div>

            {screenshots.length > 1 && (
              <div className="gdp-gallery">
                <div className="gdp-gallery-head">
                  <span className="gdp-gallery-label">{t("gameDetails.gallery" as any) || `Gallery · ${screenshots.length}`}</span>
                  <span className="gdp-gallery-count">{selectedArtIndex + 1} / {screenshots.length}</span>
                </div>
                <div className="gdp-gallery-grid">
                  {screenshots.map((url, idx) => (
                    <button
                      key={`${url}-${idx}`}
                      type="button"
                      className={`gdp-gallery-thumb${idx === selectedArtIndex ? " active" : ""}`}
                      onClick={() => setSelectedArtIndex(idx)}
                      aria-label={`Screenshot ${idx + 1}`}
                    >
                      <img src={url} alt="" loading="lazy" />
                      <span className="gdp-thumb-brackets" aria-hidden="true">
                        <span className="gdp-bracket gdp-bracket--tl" />
                        <span className="gdp-bracket gdp-bracket--tr" />
                        <span className="gdp-bracket gdp-bracket--bl" />
                        <span className="gdp-bracket gdp-bracket--br" />
                      </span>
                    </button>
                  ))}
                </div>
              </div>
            )}

            {screenshots.length <= 1 && screenshots.length > 0 && (
              <div className="gdp-screenshot-row">
                <span className="gdp-dot active" aria-hidden="true" />
              </div>
            )}

            {!isOwned && (
              <div className="gdp-store-hint">
                <AlertCircle size={14} />
                <span>{t("gameDetails.storeHint")}</span>
              </div>
            )}
          </div>
        </div>
      </div>
    </div>
  );
});
