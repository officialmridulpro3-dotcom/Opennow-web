import {
  Activity,
  CalendarDays,
  Clock,
  Gamepad2,
  Hourglass,
  Library,
  RefreshCw,
  RotateCcw,
  Timer,
  Trophy,
} from "lucide-react";
import { memo, useMemo, useState } from "react";
import type { JSX } from "react";
import type { GameInfo } from "@shared/gfn";
import type { PlaytimeGameAggregate, PlaytimeSummary } from "@shared/playtime";

import { useTranslation } from "../i18n";
import { formatCatalogLastPlayed } from "../utils/lastPlayedFormat";
import { formatPlaytimeDuration } from "../utils/playtimeFormat";
import { formatElapsed } from "../utils/timeFormat";
import { useElapsedSeconds } from "../utils/useElapsedSeconds";
import { getStoreDisplayName } from "./GameCard";

/** How many bars the day strip shows. */
const DAILY_WINDOW = 14;
/** Ranking rows before the list folds into a scroll area. */
const RANKING_LIMIT = 12;

export interface PlaytimePageProps {
  summary: PlaytimeSummary | null;
  /** Library metadata, used to resolve titles and posters for ledger entries. */
  libraryGames: GameInfo[];
  isRecording: boolean;
  streamStartedAtMs: number | null;
  recordingGameTitle: string | null;
  onRefreshPlaytime: () => Promise<void>;
  onResetPlaytime: () => Promise<void>;
  onNavigateLibrary: () => void;
}

interface RankedGame extends PlaytimeGameAggregate {
  resolvedTitle: string;
  resolvedImage: string | null;
  resolvedStore: string | null;
  sharePercent: number;
  barPercent: number;
}

function narrowWeekdayLabels(): string[] {
  const formatter = new Intl.DateTimeFormat(undefined, { weekday: "narrow" });
  // 2026-09-27 is a Sunday, so seven consecutive days give Sun..Sat in order.
  return Array.from({ length: 7 }, (_, index) => {
    const date = new Date(2026, 8, 27 + index, 12);
    return formatter.format(date);
  });
}

export const PlaytimePage = memo(function PlaytimePage({
  summary,
  libraryGames,
  isRecording,
  streamStartedAtMs,
  recordingGameTitle,
  onRefreshPlaytime,
  onResetPlaytime,
  onNavigateLibrary,
}: PlaytimePageProps): JSX.Element {
  const { t } = useTranslation();
  const [confirmResetOpen, setConfirmResetOpen] = useState(false);
  const [isBusy, setIsBusy] = useState(false);
  const liveSeconds = useElapsedSeconds(streamStartedAtMs, isRecording);
  const weekdayLabels = useMemo(narrowWeekdayLabels, []);

  const gamesById = useMemo(() => {
    const map = new Map<string, GameInfo>();
    for (const game of libraryGames) map.set(game.id, game);
    return map;
  }, [libraryGames]);

  const totals = summary?.totals ?? null;
  const streamedSeconds = totals?.streamedSeconds ?? 0;

  const ranked = useMemo<RankedGame[]>(() => {
    const games = summary?.games ?? [];
    const leader = games[0]?.totalSeconds ?? 0;
    return games.map((game) => {
      const library = gamesById.get(game.gameId);
      return {
        ...game,
        resolvedTitle: library?.title?.trim() || game.title || game.gameId,
        resolvedImage: library?.heroImageUrl || library?.imageUrl || game.imageUrl,
        resolvedStore: game.store ?? library?.availableStores?.[0] ?? null,
        sharePercent: streamedSeconds > 0 ? Math.round((game.totalSeconds / streamedSeconds) * 100) : 0,
        barPercent: leader > 0 ? Math.max(2, Math.round((game.totalSeconds / leader) * 100)) : 0,
      };
    });
  }, [gamesById, streamedSeconds, summary?.games]);

  const hourly = useMemo(() => {
    const values = summary?.hourlySeconds ?? [];
    const peak = Math.max(1, ...values);
    return Array.from({ length: 24 }, (_, hour) => ({
      hour,
      seconds: values[hour] ?? 0,
      percent: Math.round(((values[hour] ?? 0) / peak) * 100),
    }));
  }, [summary?.hourlySeconds]);

  const weekdays = useMemo(() => {
    const values = summary?.weekdaySeconds ?? [];
    const peak = Math.max(1, ...values);
    return Array.from({ length: 7 }, (_, day) => ({
      day,
      label: weekdayLabels[day] ?? "",
      seconds: values[day] ?? 0,
      percent: Math.round(((values[day] ?? 0) / peak) * 100),
    }));
  }, [summary?.weekdaySeconds, weekdayLabels]);

  const daily = useMemo(() => {
    const entries = (summary?.daily ?? []).slice(-DAILY_WINDOW);
    const peak = Math.max(1, ...entries.map((entry) => entry.seconds));
    const todayKey = new Date().toLocaleDateString("en-CA");
    return entries.map((entry) => ({
      ...entry,
      percent: Math.round((entry.seconds / peak) * 100),
      isToday: entry.date === todayKey,
      label: `${Number(entry.date.slice(8, 10))}`,
    }));
  }, [summary?.daily]);

  const recent = summary?.recent ?? [];

  const formatDuration = (seconds: number): string => formatPlaytimeDuration(t, seconds);

  const heroHours = Math.floor(streamedSeconds / 3600);
  const heroMinutes = Math.floor((streamedSeconds % 3600) / 60);
  const heroValue = heroHours > 0 ? heroHours : heroMinutes;
  const heroUnit = heroHours > 0 ? t("playtime.hero.unitHours") : t("playtime.hero.unitMinutes");

  const statCells = [
    { id: "sessions", icon: Gamepad2, label: t("playtime.stats.sessions"), value: `${totals?.sessionCount ?? 0}` },
    { id: "games", icon: Trophy, label: t("playtime.stats.games"), value: `${totals?.gamesPlayed ?? 0}` },
    { id: "longest", icon: Timer, label: t("playtime.stats.longest"), value: formatDuration(totals?.longestSessionSeconds ?? 0) },
    { id: "average", icon: Activity, label: t("playtime.stats.average"), value: formatDuration(totals?.averageSessionSeconds ?? 0) },
    { id: "queue", icon: Hourglass, label: t("playtime.stats.queue"), value: formatDuration(totals?.queueSeconds ?? 0) },
    { id: "setup", icon: Clock, label: t("playtime.stats.setup"), value: formatDuration(totals?.setupSeconds ?? 0) },
  ];

  const handleRefresh = (): void => {
    setIsBusy(true);
    void onRefreshPlaytime().finally(() => setIsBusy(false));
  };

  const handleReset = (): void => {
    setIsBusy(true);
    void onResetPlaytime().finally(() => {
      setIsBusy(false);
      setConfirmResetOpen(false);
    });
  };

  const hasHistory = streamedSeconds > 0 || (totals?.sessionCount ?? 0) > 0;
  const peakHour = hourly.reduce((best, entry) => (entry.seconds > best.seconds ? entry : best), hourly[0]);

  return (
    <section className="pt-root" aria-labelledby="playtime-heading">
      <div className="pt-ambient" aria-hidden="true">
        <span className="pt-ambient-mesh" />
        <span className="pt-ambient-bloom pt-ambient-bloom--neon" />
        <span className="pt-ambient-bloom pt-ambient-bloom--azure" />
        <span className="pt-ambient-scan" />
      </div>

      <header className="pt-masthead">
        <p className="pt-eyebrow">{t("playtime.eyebrow")}</p>
        <h1 className="pt-title" id="playtime-heading">{t("playtime.title")}</h1>
        <p className="pt-voice">{t("playtime.subtitle")}</p>
      </header>

      {isRecording && (
        <div className="pt-live" role="status">
          <span className="pt-live-pulse" aria-hidden="true" />
          <div className="pt-live-body">
            <p className="pt-live-label">{t("playtime.live.label")}</p>
            <p className="pt-live-title">{recordingGameTitle ?? t("playtime.live.unknownGame")}</p>
          </div>
          <p className="pt-live-clock">{formatElapsed(liveSeconds)}</p>
          <span className="pt-live-segments" aria-hidden="true">
            <i /><i /><i /><i /><i />
          </span>
        </div>
      )}

      {!summary && (
        <p className="pt-note">{t("playtime.loading")}</p>
      )}

      {summary && !hasHistory && !isRecording && (
        <div className="pt-empty">
          <p className="pt-eyebrow">{t("playtime.empty.eyebrow")}</p>
          <h2 className="pt-empty-title">{t("playtime.empty.title")}</h2>
          <p className="pt-empty-body">{t("playtime.empty.body")}</p>
          <button type="button" className="pt-btn pt-btn-primary" onClick={onNavigateLibrary}>
            <Library size={18} />
            {t("playtime.empty.cta")}
          </button>
        </div>
      )}

      {hasHistory && (
        <>
          <div className="pt-hero">
            <div className="pt-hero-total">
              <p className="pt-hero-label">{t("playtime.hero.label")}</p>
              <p className="pt-hero-numeral">
                {heroValue}
                <span className="pt-hero-unit">{heroUnit}</span>
              </p>
              <span className="pt-sr-only">
                {t("playtime.hero.sr", { hours: heroHours, minutes: heroMinutes })}
              </span>
              <p className="pt-hero-detail">
                {t("playtime.hero.detail", {
                  minutes: heroMinutes,
                  sessions: totals?.sessionCount ?? 0,
                  games: totals?.gamesPlayed ?? 0,
                })}
              </p>
              {peakHour && peakHour.seconds > 0 && (
                <p className="pt-hero-footnote">
                  {t("playtime.hero.peakHour", {
                    hour: `${peakHour.hour}`.padStart(2, "0"),
                    value: formatDuration(peakHour.seconds),
                  })}
                </p>
              )}
            </div>

            <ul className="pt-stat-grid">
              {statCells.map((cell) => {
                const Icon = cell.icon;
                return (
                  <li className="pt-cell" key={cell.id}>
                    <span className="pt-cell-head">
                      <Icon size={14} aria-hidden="true" />
                      {cell.label}
                    </span>
                    <span className="pt-cell-value">{cell.value}</span>
                    <span className="pt-cell-tick" aria-hidden="true" />
                  </li>
                );
              })}
            </ul>
          </div>

          <section className="pt-block" aria-labelledby="playtime-ranking">
            <h2 className="pt-block-title" id="playtime-ranking">
              {t("playtime.ranking.title")}
              <span className="pt-block-count">{t("playtime.ranking.count", { count: ranked.length })}</span>
            </h2>
            <ol className="pt-rank-list">
              {ranked.slice(0, RANKING_LIMIT).map((game, index) => (
                <li className="pt-rank-row" key={game.gameId}>
                  <span className={`pt-rank-index${index === 0 ? " lead" : ""}`} aria-hidden="true">
                    {index + 1}
                  </span>
                  <span
                    className="pt-rank-art"
                    style={game.resolvedImage ? { backgroundImage: `url(${game.resolvedImage})` } : undefined}
                    aria-hidden="true"
                  />
                  <div className="pt-rank-body">
                    <p className="pt-rank-name">{game.resolvedTitle}</p>
                    <p className="pt-rank-meta">
                      {t("playtime.ranking.sessions", { count: game.sessionCount })}
                      {game.resolvedStore ? ` · ${getStoreDisplayName(game.resolvedStore)}` : ""}
                      {` · ${formatCatalogLastPlayed(t, game.lastPlayedAt ?? undefined)}`}
                    </p>
                    <span className="pt-conduit" aria-hidden="true">
                      <span className="pt-conduit-fill" style={{ width: `${game.barPercent}%` }} />
                    </span>
                  </div>
                  <div className="pt-rank-total">
                    <span className="pt-rank-hours">{formatDuration(game.totalSeconds)}</span>
                    <span className="pt-rank-share">
                      {t("playtime.ranking.share", { value: game.sharePercent })}
                    </span>
                    <span className="pt-sr-only">
                      {t("playtime.ranking.sr", {
                        rank: index + 1,
                        title: game.resolvedTitle,
                        value: formatDuration(game.totalSeconds),
                        sessions: game.sessionCount,
                      })}
                    </span>
                  </div>
                </li>
              ))}
            </ol>
            {ranked.length > RANKING_LIMIT && (
              <p className="pt-note">
                {t("playtime.ranking.more", { count: ranked.length - RANKING_LIMIT })}
              </p>
            )}
          </section>

          <section className="pt-block" aria-labelledby="playtime-cadence">
            <h2 className="pt-block-title" id="playtime-cadence">{t("playtime.cadence.title")}</h2>

            <div className="pt-cadence-grid">
            <div className="pt-panel">
              <p className="pt-panel-label">
                <CalendarDays size={14} aria-hidden="true" />
                {t("playtime.cadence.daily", { count: daily.length })}
              </p>
              <div className="pt-strip" aria-hidden="true">
                {daily.map((entry) => (
                  <span className={`pt-strip-cell${entry.isToday ? " today" : ""}`} key={entry.date}>
                    <i style={{ height: `${Math.max(4, entry.percent)}%` }} />
                    <em>{entry.label}</em>
                  </span>
                ))}
              </div>
              <p className="pt-sr-only">
                {t("playtime.cadence.dailySr", { value: formatDuration(daily.reduce((sum, entry) => sum + entry.seconds, 0)) })}
              </p>
            </div>

            <div className="pt-panel">
              <p className="pt-panel-label">
                <Clock size={14} aria-hidden="true" />
                {t("playtime.cadence.hourly")}
              </p>
              <div className="pt-chart" aria-hidden="true">
                {hourly.map((entry) => (
                  <span className="pt-chart-bar" key={entry.hour} data-hour={entry.hour}>
                    <i style={{ height: `${entry.seconds > 0 ? Math.max(4, entry.percent) : 2}%` }} />
                  </span>
                ))}
              </div>
              <p className="pt-sr-only">
                {peakHour && peakHour.seconds > 0
                  ? t("playtime.cadence.hourlySr", { hour: `${peakHour.hour}`.padStart(2, "0"), value: formatDuration(peakHour.seconds) })
                  : t("playtime.format.never")}
              </p>
            </div>

            <div className="pt-panel">
              <p className="pt-panel-label">
                <Activity size={14} aria-hidden="true" />
                {t("playtime.cadence.weekday")}
              </p>
              <div className="pt-week" aria-hidden="true">
                {weekdays.map((entry) => (
                  <span className="pt-week-cell" key={entry.day}>
                    <i style={{ height: `${entry.seconds > 0 ? Math.max(6, entry.percent) : 3}%` }} />
                    <em>{entry.label}</em>
                  </span>
                ))}
              </div>
              <p className="pt-sr-only">
                {t("playtime.cadence.weekdaySr", {
                  value: formatDuration(Math.max(...weekdays.map((entry) => entry.seconds), 0)),
                })}
              </p>
            </div>
            </div>
          </section>

          <section className="pt-block" aria-labelledby="playtime-recent">
            <h2 className="pt-block-title" id="playtime-recent">{t("playtime.recent.title")}</h2>
            {recent.length === 0 ? (
              <p className="pt-note">{t("playtime.recent.empty")}</p>
            ) : (
              <ul className="pt-recent">
                {recent.map((entry) => {
                  const library = gamesById.get(entry.gameId);
                  const title = library?.title?.trim() || entry.title || entry.gameId;
                  const started = new Date(entry.startedAt);
                  return (
                    <li className="pt-recent-row" key={entry.playbackId}>
                      <span className="pt-recent-when">
                        <strong>{Number.isFinite(started.getTime()) ? started.toLocaleDateString() : "—"}</strong>
                        <em>{Number.isFinite(started.getTime()) ? started.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" }) : ""}</em>
                      </span>
                      <span className="pt-recent-title">{title}</span>
                      <span className="pt-recent-value">{formatDuration(entry.seconds)}</span>
                      <span className="pt-recent-wait">
                        {t("playtime.recent.wait", { value: formatDuration(entry.queueSeconds + entry.setupSeconds) })}
                      </span>
                      {entry.clientMode && (
                        <span className="pt-chip">{entry.clientMode === "native" ? t("playtime.recent.engineNative") : t("playtime.recent.engineWeb")}</span>
                      )}
                    </li>
                  );
                })}
              </ul>
            )}
          </section>
        </>
      )}

      <footer className="pt-actions">
        {summary?.updatedAt && (
          <p className="pt-actions-note">
            {t("playtime.actions.updated", { value: new Date(summary.updatedAt).toLocaleString() })}
          </p>
        )}
        {confirmResetOpen ? (
          <div className="pt-confirm" role="group" aria-label={t("playtime.actions.resetConfirmTitle")}>
            <p className="pt-confirm-text">{t("playtime.actions.resetConfirmBody")}</p>
            <button type="button" className="pt-btn pt-btn-danger" onClick={handleReset} disabled={isBusy}>
              <RotateCcw size={16} />
              {t("playtime.actions.resetConfirm")}
            </button>
            <button
              type="button"
              className="pt-btn pt-btn-ghost"
              onClick={() => setConfirmResetOpen(false)}
              disabled={isBusy}
            >
              {t("playtime.actions.resetCancel")}
            </button>
          </div>
        ) : (
          <div className="pt-actions-row">
            <button type="button" className="pt-btn pt-btn-ghost" onClick={handleRefresh} disabled={isBusy}>
              <RefreshCw size={16} />
              {t("playtime.actions.refresh")}
            </button>
            {hasHistory && (
              <button
                type="button"
                className="pt-btn pt-btn-quiet"
                onClick={() => setConfirmResetOpen(true)}
                disabled={isBusy}
              >
                <RotateCcw size={16} />
                {t("playtime.actions.reset")}
              </button>
            )}
          </div>
        )}
      </footer>
    </section>
  );
});
