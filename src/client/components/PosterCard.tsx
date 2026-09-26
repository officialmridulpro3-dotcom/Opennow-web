import { Play, Monitor } from "lucide-react";
import { memo, type JSX } from "react";
import { m } from "motion/react";
import type { GameInfo } from "@shared/gfn";
import { getStoreDisplayName, getStoreIconComponent } from "./GameCard";
import { useTranslation } from "../i18n";

export interface PosterCardProps {
  game: GameInfo;
  isSelected?: boolean;
  onSelect: () => void;
  onPlay: () => void;
  subtitle?: string;
}

function getPosterUrl(game: GameInfo): string | undefined {
  return (
    game.imageUrlsByType?.GAME_BOX_ART?.[0]
    ?? game.imageUrlsByType?.KEY_ART?.[0]
    ?? game.imageUrl
    ?? game.imageUrlsByType?.KEY_IMAGE?.[0]
    ?? game.heroImageUrl
  );
}

function getActiveStoreRaw(game: GameInfo): string | undefined {
  const variant = game.variants[game.selectedVariantIndex] ?? game.variants[0];
  return variant?.store ?? game.availableStores?.[0];
}

export const PosterCard = memo(function PosterCard({
  game,
  isSelected = false,
  onSelect,
  onPlay,
  subtitle,
}: PosterCardProps): JSX.Element {
  const { t } = useTranslation();
  const posterUrl = getPosterUrl(game);
  const storeRaw = getActiveStoreRaw(game);
  const storeLabel = subtitle ?? (storeRaw ? getStoreDisplayName(storeRaw) : undefined);
  const StoreIcon = !subtitle && storeRaw ? getStoreIconComponent(storeRaw) : null;

  return (
    <m.div
      className={`poster-card${isSelected ? " selected" : ""}`}
      onClick={onSelect}
      onDoubleClick={onPlay}
      onKeyDown={(event) => {
        if (event.target !== event.currentTarget) return;
        if (event.key === "Enter" || event.key === " ") {
          event.preventDefault();
          onPlay();
        }
      }}
      role="button"
      tabIndex={0}
      aria-label={t("gameCard.selectGame", { title: game.title })}
      whileHover={{ y: -5, scale: 1.02 }}
      whileTap={{ scale: 0.98 }}
      transition={{ type: "spring", stiffness: 420, damping: 30 }}
    >
      <div className="poster-card-art">
        {posterUrl ? (
          <img src={posterUrl} alt={game.title} className="poster-card-img" loading="lazy" />
        ) : (
          <div className="poster-card-placeholder">
            <Monitor size={34} />
            <span>{game.title}</span>
          </div>
        )}
        <div className="poster-card-scrim" />
        <div className="poster-card-body">
          <p className="poster-card-title" title={game.title}>{game.title}</p>
          {storeLabel && (
            <p className="poster-card-meta">
              {StoreIcon && (
                <span className="poster-card-meta-icon"><StoreIcon /></span>
              )}
              <span>{storeLabel}</span>
            </p>
          )}
          <span className="poster-card-playwrap" aria-hidden="true">
            <span className="poster-card-playwrap-inner">
              <button
                type="button"
                className="poster-card-play"
                onClick={(event) => {
                  event.stopPropagation();
                  onPlay();
                }}
                tabIndex={-1}
                aria-label={t("gameCard.playGame", { title: game.title })}
              >
                <Play size={13} fill="currentColor" />
                <span>{t("app.actions.play")}</span>
              </button>
            </span>
          </span>
        </div>
      </div>
    </m.div>
  );
});
