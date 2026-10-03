/**
 * MES (Membership/Subscription) API integration for GeForce NOW
 * Handles fetching subscription info from the MES API endpoint.
 */

import type {
  SubscriptionInfo,
  EntitledResolution,
  StorageAddon,
  StreamRegion,
} from "@shared/gfn";

import { buildGfnLcarsHeaders } from "./clientHeaders";

/** MES API endpoint URL */
const MES_URL = "https://mes.geforcenow.com/v4/subscriptions";

interface SubscriptionResponse {
  firstEntitlementStartDateTime?: string;
  type?: string;
  membershipTier?: string;
  allottedTimeInMinutes?: number;
  purchasedTimeInMinutes?: number;
  rolledOverTimeInMinutes?: number;
  remainingTimeInMinutes?: number;
  totalTimeInMinutes?: number;
  notifications?: {
    notifyUserWhenTimeRemainingInMinutes?: number;
    notifyUserOnSessionWhenRemainingTimeInMinutes?: number;
  };
  currentSpanStartDateTime?: string;
  currentSpanEndDateTime?: string;
  currentSubscriptionState?: {
    state?: string;
    isGamePlayAllowed?: boolean;
  };
  subType?: string;
  addons?: SubscriptionAddonResponse[];
  features?: SubscriptionFeatures;
}

interface SubscriptionFeatures {
  resolutions?: SubscriptionResolution[];
}

interface SubscriptionResolution {
  heightInPixels: number;
  widthInPixels: number;
  framesPerSecond: number;
  isEntitled: boolean;
}

interface SubscriptionAddonResponse {
  type?: string;
  subType?: string;
  status?: string;
  attributes?: AddonAttribute[];
}

interface AddonAttribute {
  key?: string;
  textValue?: string;
}

function parseMinutes(value: unknown): number | undefined {
  if (typeof value === "number" && Number.isFinite(value)) {
    return value;
  }
  if (typeof value === "string" && value.trim().length > 0) {
    const parsed = Number(value);
    if (Number.isFinite(parsed)) {
      return parsed;
    }
  }
  return undefined;
}

function parseNumberText(value: unknown): number | undefined {
  if (typeof value !== "string" || value.trim().length === 0) {
    return undefined;
  }
  const parsed = Number(value);
  if (!Number.isFinite(parsed)) {
    return undefined;
  }
  return parsed;
}

function parseIsoDate(value: unknown): string | undefined {
  return typeof value === "string" && value.length > 0 ? value : undefined;
}

/**
 * Fetch subscription info from MES API
 * @param token - The authentication token
 * @param userId - The user ID
 * @param vpcId - The VPC ID (defaults to a common European VPC if not provided)
 * @returns The subscription info
 */
export async function fetchSubscription(
  token: string,
  userId: string,
  vpcId = "NP-AMS-08",
): Promise<SubscriptionInfo> {
  const url = new URL(MES_URL);
  url.searchParams.append("serviceName", "gfn_pc");
  url.searchParams.append("languageCode", "en_US");
  url.searchParams.append("vpcId", vpcId);
  url.searchParams.append("userId", userId);

  const response = await fetch(url.toString(), {
    headers: buildGfnLcarsHeaders({
      token,
      clientType: "NATIVE",
      clientStreamer: "NVIDIA-CLASSIC",
    }),
  });

  if (!response.ok) {
    const body = await response.text();
    throw new Error(`Subscription API failed with status ${response.status}: ${body}`);
  }

  const data = (await response.json()) as SubscriptionResponse;

  // Parse membership tier (defaults to FREE)
  const membershipTier = data.membershipTier ?? "FREE";

  // Convert minutes to hours. Use the additive fields as fallback if total is absent.
  const allottedMinutes = parseMinutes(data.allottedTimeInMinutes) ?? 0;
  const purchasedMinutes = parseMinutes(data.purchasedTimeInMinutes) ?? 0;
  const rolledOverMinutes = parseMinutes(data.rolledOverTimeInMinutes) ?? 0;
  const fallbackTotalMinutes = allottedMinutes + purchasedMinutes + rolledOverMinutes;
  const totalMinutes = parseMinutes(data.totalTimeInMinutes) ?? fallbackTotalMinutes;
  const remainingMinutes = parseMinutes(data.remainingTimeInMinutes) ?? 0;
  const usedMinutes = Math.max(totalMinutes - remainingMinutes, 0);

  const allottedHours = allottedMinutes / 60;
  const purchasedHours = purchasedMinutes / 60;
  const rolledOverHours = rolledOverMinutes / 60;
  const usedHours = usedMinutes / 60;
  const remainingHours = remainingMinutes / 60;
  const totalHours = totalMinutes / 60;

  // Check if unlimited subscription
  const isUnlimited = data.subType === "UNLIMITED";

  // Parse storage addon
  let storageAddon: StorageAddon | undefined;
  const storageAddonResponse = data.addons?.find(
    (addon) =>
      addon.type === "STORAGE" &&
      addon.subType === "PERMANENT_STORAGE" &&
      addon.status === "OK",
  );

  if (storageAddonResponse) {
    const sizeAttr = storageAddonResponse.attributes?.find(
      (attr) => attr.key === "TOTAL_STORAGE_SIZE_IN_GB",
    );
    const usedAttr = storageAddonResponse.attributes?.find(
      (attr) => attr.key === "USED_STORAGE_SIZE_IN_GB",
    );
    const regionNameAttr = storageAddonResponse.attributes?.find(
      (attr) => attr.key === "STORAGE_METRO_REGION_NAME",
    );
    const regionCodeAttr = storageAddonResponse.attributes?.find(
      (attr) => attr.key === "STORAGE_METRO_REGION",
    );
    const sizeGb = parseNumberText(sizeAttr?.textValue);
    const usedGb = parseNumberText(usedAttr?.textValue);
    const regionName = regionNameAttr?.textValue;
    const regionCode = regionCodeAttr?.textValue;

    storageAddon = {
      type: "PERMANENT_STORAGE",
      sizeGb,
      usedGb,
      regionName,
      regionCode,
    };
  }

  // Parse entitled resolutions
  const entitledResolutions: EntitledResolution[] = [];
  if (data.features?.resolutions) {
    for (const res of data.features.resolutions) {
      if (!res.isEntitled) {
        continue;
      }
      entitledResolutions.push({
        width: res.widthInPixels,
        height: res.heightInPixels,
        fps: res.framesPerSecond,
      });
    }

    // Sort by highest resolution/fps first
    entitledResolutions.sort((a, b) => {
      if (b.width !== a.width) return b.width - a.width;
      if (b.height !== a.height) return b.height - a.height;
      return b.fps - a.fps;
    });
  }

  return {
    membershipTier,
    subscriptionType: data.type,
    subscriptionSubType: data.subType,
    allottedHours,
    purchasedHours,
    rolledOverHours,
    usedHours,
    remainingHours,
    totalHours,
    firstEntitlementStartDateTime: parseIsoDate(data.firstEntitlementStartDateTime),
    serverRegionId: vpcId,
    currentSpanStartDateTime: parseIsoDate(data.currentSpanStartDateTime),
    currentSpanEndDateTime: parseIsoDate(data.currentSpanEndDateTime),
    notifyUserWhenTimeRemainingInMinutes: parseMinutes(
      data.notifications?.notifyUserWhenTimeRemainingInMinutes,
    ),
    notifyUserOnSessionWhenRemainingTimeInMinutes: parseMinutes(
      data.notifications?.notifyUserOnSessionWhenRemainingTimeInMinutes,
    ),
    state: data.currentSubscriptionState?.state,
    isGamePlayAllowed: data.currentSubscriptionState?.isGamePlayAllowed,
    isUnlimited,
    storageAddon,
    entitledResolutions,
  };
}

/**
 * Fetch dynamic regions from serverInfo endpoint to get VPC ID
 * @param token - Optional authentication token
 * @param streamingBaseUrl - Base URL for the streaming service
 * @returns Array of stream regions and the discovered VPC ID
 */
/**
 * Static fallback list of known GFN CloudMatch zones.
 * Used when serverInfo fails or returns empty, so user can always see regions.
 * URLs follow pattern https://{zone}.cloudmatchbeta.nvidiagrid.net/
 */
const FALLBACK_REGION_ZONES: Array<{ name: string; zone: string }> = [
  { name: "US Central - Dallas", zone: "np-dal-08" },
  { name: "US East - Ashburn", zone: "np-ash-08" },
  { name: "US Midwest - Chicago", zone: "np-chi-08" },
  { name: "US Northeast - Newark", zone: "np-nwk-08" },
  { name: "US Northwest - Seattle", zone: "np-sea-08" },
  { name: "US South - Atlanta", zone: "np-atl-08" },
  { name: "US Southeast - Miami", zone: "np-mia-08" },
  { name: "US Southwest - Los Angeles", zone: "np-lax-08" },
  { name: "US West - San Jose", zone: "np-sjc-08" },
  { name: "CA East - Montreal", zone: "np-yul-08" },
  { name: "EU Northeast - Amsterdam", zone: "np-ams-08" },
  { name: "EU Central - Frankfurt", zone: "np-frk-08" },
  { name: "EU Southwest - Paris", zone: "np-par-08" },
  { name: "EU West - London", zone: "np-lhr-08" },
  { name: "EU Northwest - Stockholm", zone: "np-sto-08" },
  { name: "EU Southeast - Sofia", zone: "np-sof-02" },
  { name: "EU Southeast - Warsaw", zone: "np-waw-02" },
  { name: "Caucasus South - Yerevan", zone: "np-evn-02" },
  { name: "Africa South - Johannesburg", zone: "np-jnb-02" },
  { name: "SA East - Sao Paulo", zone: "np-gru-08" },
  { name: "LATAM South - Montevideo", zone: "np-mvd-02" },
  { name: "AU East - Sydney", zone: "np-syd-02" },
  { name: "AU West - Perth", zone: "np-per-02" },
  { name: "JP East - Tokyo", zone: "np-nrt-08" },
  { name: "SG - Singapore", zone: "np-sin-02" },
  { name: "KR - Seoul", zone: "np-icn-02" },
  { name: "TR Central - Ankara", zone: "np-ank-02" },
  { name: "TR West - Istanbul", zone: "np-ist-02" },
  { name: "TW - Taipei", zone: "np-tpe-02" },
  { name: "Netherlands North", zone: "np-ams-08" },
  { name: "Netherlands South", zone: "np-ams-09" },
  { name: "Germany North", zone: "np-frk-09" },
  { name: "UK South", zone: "np-lhr-09" },
  { name: "France Central", zone: "np-par-09" },
];

function buildFallbackRegions(): StreamRegion[] {
  const seen = new Set<string>();
  const out: StreamRegion[] = [];
  for (const { name, zone } of FALLBACK_REGION_ZONES) {
    const url = `https://${zone}.cloudmatchbeta.nvidiagrid.net/`;
    if (seen.has(url)) continue;
    seen.add(url);
    out.push({ name, url });
  }
  return out.sort((a, b) => a.name.localeCompare(b.name));
}

export async function fetchDynamicRegions(
  token: string | undefined,
  streamingBaseUrl: string,
): Promise<{ regions: StreamRegion[]; vpcId: string | null }> {
  const base = streamingBaseUrl.endsWith("/")
    ? streamingBaseUrl
    : `${streamingBaseUrl}/`;
  const url = `${base}v2/serverInfo`;

  const headers = buildGfnLcarsHeaders({
    token,
    clientType: "BROWSER",
    clientStreamer: "WEBRTC",
  });

  let response: Response;
  try {
    response = await fetch(url, { headers });
  } catch (err) {
    console.warn("[Regions] serverInfo fetch failed, using fallback:", err);
    return { regions: buildFallbackRegions(), vpcId: null };
  }

  if (!response.ok) {
    console.warn(`[Regions] serverInfo ${response.status}, using fallback`);
    return { regions: buildFallbackRegions(), vpcId: null };
  }

  let data: {
    requestStatus?: { serverId?: string };
    metaData?: Array<{ key: string; value: string }>;
  };
  try {
    data = (await response.json()) as typeof data;
  } catch (err) {
    console.warn("[Regions] serverInfo JSON parse failed, using fallback:", err);
    return { regions: buildFallbackRegions(), vpcId: null };
  }

  const vpcId = data.requestStatus?.serverId ?? null;
  const meta = data.metaData ?? [];
  const byKey = new Map(meta.map((e) => [e.key, e.value]));

  // Primary: parse gfn-regions list if present (most reliable)
  const regionsFromList: StreamRegion[] = [];
  const gfnRegionsRaw = byKey.get("gfn-regions");
  if (gfnRegionsRaw) {
    const names = gfnRegionsRaw
      .split(",")
      .map((n) => n.trim())
      .filter(Boolean);
    for (const name of names) {
      const urlRaw = byKey.get(name);
      if (!urlRaw?.startsWith("https://")) continue;
      regionsFromList.push({
        name,
        url: urlRaw.endsWith("/") ? urlRaw : `${urlRaw}/`,
      });
    }
  }

  // Secondary: all https entries that are not gfn- prefixed (legacy)
  const regionsFromHttps = meta
    .filter(
      (entry) =>
        entry.value.startsWith("https://") &&
        entry.key !== "gfn-regions" &&
        !entry.key.startsWith("gfn-") &&
        !entry.key.startsWith("local-"),
    )
    .map<StreamRegion>((entry) => ({
      name: entry.key,
      url: entry.value.endsWith("/") ? entry.value : `${entry.value}/`,
    }));

  // Merge both, dedupe by url, keep best name
  const merged = new Map<string, StreamRegion>();
  for (const r of [...regionsFromList, ...regionsFromHttps]) {
    if (!merged.has(r.url)) {
      merged.set(r.url, r);
    }
  }

  let regions = [...merged.values()].sort((a, b) => a.name.localeCompare(b.name));

  // If still empty or only 1 region (like Netherlands North), use fallback + merge
  if (regions.length <= 1) {
    const fallback = buildFallbackRegions();
    for (const fr of fallback) {
      if (!merged.has(fr.url)) {
        merged.set(fr.url, fr);
      }
    }
    regions = [...merged.values()].sort((a, b) => a.name.localeCompare(b.name));
    console.log(`[Regions] using fallback merged, total ${regions.length} (had ${regionsFromList.length} from list, ${regionsFromHttps.length} from https)`);
  }

  // If still empty, return pure fallback
  if (regions.length === 0) {
    regions = buildFallbackRegions();
  }

  return { regions, vpcId };
}
