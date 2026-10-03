import type { CatalogFilterGroup, CatalogSortOption, GameInfo } from "@shared/gfn";

const CATALOG_SNAPSHOT_KEY = "opennow.catalogSnapshot.v1";

export interface CatalogSnapshot {
  version: 1;
  userId: string;
  streamingBaseUrl: string;
  queryKey: string;
  games: GameInfo[];
  libraryGames: GameInfo[];
  filterGroups: CatalogFilterGroup[];
  sortOptions: CatalogSortOption[];
  totalCount: number;
  supportedCount: number;
  savedAt: number;
}

export function buildCatalogQueryKey(
  searchQuery: string,
  filterIds: string[],
  sortId: string,
): string {
  return `${searchQuery.trim()}|${filterIds.join("|")}|${sortId}`;
}

export function loadCatalogSnapshot(
  userId: string,
  streamingBaseUrl: string,
  queryKey: string,
): CatalogSnapshot | null {
  try {
    const raw = localStorage.getItem(CATALOG_SNAPSHOT_KEY);
    if (!raw) return null;

    const parsed = JSON.parse(raw) as Partial<CatalogSnapshot>;
    if (
      parsed.version !== 1
      || !Array.isArray(parsed.games)
      || parsed.games.length === 0
    ) {
      return null;
    }

    // Strict match first
    const strictMatch =
      parsed.userId === userId &&
      parsed.streamingBaseUrl === streamingBaseUrl &&
      parsed.queryKey === queryKey;

    // Lenient match: same user, allow different base/query for instant load
    const lenientMatch =
      parsed.userId === userId &&
      Array.isArray(parsed.games) &&
      parsed.games.length > 0;

    if (!strictMatch && !lenientMatch) {
      return null;
    }

    // If lenient but not strict, still return for instant load, but log
    if (!strictMatch && lenientMatch) {
      console.log("[CatalogSnapshot] lenient hit (instant load) - queryKey mismatch but same user");
    }

    return {
      version: 1,
      userId: parsed.userId as string,
      streamingBaseUrl: (parsed.streamingBaseUrl as string) ?? streamingBaseUrl,
      queryKey: (parsed.queryKey as string) ?? queryKey,
      games: parsed.games as any,
      libraryGames: Array.isArray(parsed.libraryGames) ? parsed.libraryGames : [],
      filterGroups: Array.isArray(parsed.filterGroups) ? parsed.filterGroups : [],
      sortOptions: Array.isArray(parsed.sortOptions) ? parsed.sortOptions : [],
      totalCount: typeof parsed.totalCount === "number" ? parsed.totalCount : (parsed.games as any).length,
      supportedCount: typeof parsed.supportedCount === "number" ? parsed.supportedCount : (parsed.games as any).length,
      savedAt: typeof parsed.savedAt === "number" ? parsed.savedAt : 0,
    };
  } catch {
    return null;
  }
}

export function loadAnyCatalogSnapshot(): CatalogSnapshot | null {
  try {
    const raw = localStorage.getItem(CATALOG_SNAPSHOT_KEY);
    if (!raw) return null;
    const parsed = JSON.parse(raw) as Partial<CatalogSnapshot>;
    if (parsed.version !== 1 || !Array.isArray(parsed.games) || parsed.games.length === 0) return null;
    return {
      version: 1,
      userId: (parsed.userId as string) ?? "",
      streamingBaseUrl: (parsed.streamingBaseUrl as string) ?? "",
      queryKey: (parsed.queryKey as string) ?? "",
      games: parsed.games as any,
      libraryGames: Array.isArray(parsed.libraryGames) ? parsed.libraryGames : [],
      filterGroups: Array.isArray(parsed.filterGroups) ? parsed.filterGroups : [],
      sortOptions: Array.isArray(parsed.sortOptions) ? parsed.sortOptions : [],
      totalCount: typeof parsed.totalCount === "number" ? parsed.totalCount : (parsed.games as any).length,
      supportedCount: typeof parsed.supportedCount === "number" ? parsed.supportedCount : (parsed.games as any).length,
      savedAt: typeof parsed.savedAt === "number" ? parsed.savedAt : 0,
    };
  } catch {
    return null;
  }
}

export function saveCatalogSnapshot(snapshot: CatalogSnapshot): void {
  try {
    localStorage.setItem(CATALOG_SNAPSHOT_KEY, JSON.stringify(snapshot));
  } catch (error) {
    console.warn("Failed to persist catalog snapshot:", error);
  }
}

export function clearCatalogSnapshot(): void {
  try {
    localStorage.removeItem(CATALOG_SNAPSHOT_KEY);
  } catch {
    // ignore
  }
}
