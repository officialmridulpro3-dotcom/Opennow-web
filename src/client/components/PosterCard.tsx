import { Play, Monitor } from "lucide-react";
import { memo, type JSX } from "react";
import type { GameInfo } from "@shared/gfn";
import { getStoreDisplayName, getStoreIconComponent } from "./GameCard";
import { useTranslation } from "../i18n";

export interface PosterCardProps {
  game: GameInfo;
  isSelected?: boolean;
  onSelect: () => void;
  onPlay: () => void;
  subtitle?: string;
  /** Deck rank numeral, shown on ranked shelves such as Most played. */
  rank?: number;
  /** Optional ledger line under the title (playtime, last played…). */
  note?: string;
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

/**
 * Lightweight library card: bounded box art with title, store and launch
 * controls below it. Art, title and launch are sibling buttons — nothing is
 * nested inside another interactive element.
 */
export const PosterCard = memo(function PosterCard({
  game,
  isSelected = false,
  onSelect,
  onPlay,
  subtitle,
  rank,
  note,
}: PosterCardProps): JSX.Element {
  const { t } = useTranslation();
  const posterUrl = getPosterUrl(game);
  const storeRaw = getActiveStoreRaw(game);
  const storeLabel = subtitle ?? (storeRaw ? getStoreDisplayName(storeRaw) : undefined);
  const StoreIcon = !subtitle && storeRaw ? getStoreIconComponent(storeRaw) : null;

  return (
    <article className={`poster-card${isSelected ? " selected" : ""}`}>
      <div className="poster-card-art">
        <button
          type="button"
          className="poster-card-art-button"
          onClick={onSelect}
          onDoubleClick={onPlay}
          aria-pressed={isSelected}
          aria-label={t("gameCard.selectGame", { title: game.title })}
        >
          {posterUrl ? (
            <img src={posterUrl} alt="" className="poster-card-img" loading="lazy" decoding="async" />
          ) : (
            <span className="poster-card-placeholder">
              <Monitor size={30} />
              <span>{game.title}</span>
            </span>
          )}
        </button>
      </div>

      <div className="poster-card-strip">
        <div className="poster-card-strip-head">
          {typeof rank === "number" && (
            <span className="poster-card-rank" aria-hidden="true">
              {rank}
            </span>
          )}
          <button
            type="button"
            className="poster-card-title"
            onClick={onSelect}
            title={game.title}
            aria-pressed={isSelected}
          >
            {game.title}
          </button>
        </div>

        <div className="poster-card-strip-row">
          {storeLabel ? (
            <span className="poster-card-meta" title={storeLabel}>
              {StoreIcon && (
                <span className="poster-card-meta-icon">
                  <StoreIcon />
                </span>
              )}
              <span>{storeLabel}</span>
            </span>
          ) : (
            <span className="poster-card-meta" />
          )}
          <button
            type="button"
            className="poster-card-play"
            onClick={onPlay}
            aria-label={t("gameCard.playGame", { title: game.title })}
          >
            <Play size={12} fill="currentColor" />
            <span>{t("app.actions.play")}</span>
          </button>
        </div>

        {note && (
          <p className="poster-card-note">
            <span className="poster-card-note-dash" aria-hidden="true" />
            {note}
          </p>
        )}
      </div>
    </article>
  );
});
