type TranslateFunction = typeof import("../i18n").t;

/**
 * Shared duration formatter for streamed playtime.
 * Keeps the home spotlight and the playtime ledger in lockstep.
 */
export function formatPlaytimeDuration(t: TranslateFunction, seconds: number): string {
  if (!Number.isFinite(seconds) || seconds <= 0) return t("playtime.format.never");
  const hours = Math.floor(seconds / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  if (hours > 0 && minutes > 0) return t("playtime.format.hoursMinutes", { hours, minutes });
  if (hours > 0) return t("playtime.format.hours", { count: hours });
  if (minutes > 0) return t("playtime.format.minutes", { count: minutes });
  return t("playtime.format.seconds", { count: Math.max(1, Math.round(seconds)) });
}
