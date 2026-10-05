import type {
  AuthDeviceLoginChallenge,
  AuthSession,
  GameInfo,
  IceCandidatePayload,
  KeyframeRequest,
  MainToRendererSignalingEvent,
  OpenNowApi,
  PingResult,
  RecordingEntry,
  ScreenshotEntry,
  SendAnswerRequest,
  Settings,
  SignalingConnectRequest,
} from "@shared/gfn";
import {
  createUnsupportedNativeStreamerStatus,
} from "@shared/gfn";
import { unsupportedNativeCloudGsyncCapabilities } from "@shared/cloudGsync";
import type {
  LegacyPlaytimeRecord,
  PlaytimeImportPayload,
  PlaytimeSessionPayload,
  PlaytimeSummary,
} from "@shared/playtime";
import type { BrowserSession, DeviceLoginChallengePayload, DeviceLoginPollPayload } from "./types";
import { WEB_DEFAULT_SETTINGS } from "./webDefaults";

const CLIENT_LOG_BATCH_LIMIT = 200;
const CLIENT_LOG_LINE_LIMIT = 2000;
const clientLogBuffer: string[] = [];
let clientLogFlushTimer: number | null = null;

export interface NativeSidecarStatus {
  supported: boolean;
  running: boolean;
  pid?: number;
  sessionId?: string;
  exitCode?: number | null;
  lastError?: string;
  capabilities?: unknown;
  phase?: "handshake" | "starting";
  /** True once the engine logs its first inbound video datagram or decoded frame. */
  firstFrame?: boolean;
  /** Active native MKV recording (engine Ctrl+G menu / F12), if any. */
  recording?: { path: string; startedAtMs: number };
  /** Engine confirmed it embedded its surface inside the app window. */
  surfaceAttached?: boolean;
  /** Engine marker explaining a failed embed (see `server/nativeStream.ts`). */
  surfaceError?: string;
}

let cachedNativeSidecarSupport: boolean | null = null;

/**
 * Whether the backend has a bundled NVST sidecar. Fetched once per page load
 * (sidecar presence cannot change without restarting the backend) so launch
 * decisions can fall back to WebRTC on sidecar-less hosts even though native
 * streaming is the default client mode.
 */
export function getCachedNativeSidecarSupport(): Promise<boolean> {
  if (cachedNativeSidecarSupport === null) {
    cachedNativeSidecarSupport = false;
    return getNativeStatus().then(
      (status) => {
        cachedNativeSidecarSupport = status.supported === true;
        return cachedNativeSidecarSupport;
      },
      () => false,
    );
  }
  return Promise.resolve(cachedNativeSidecarSupport);
}

export function getNativeStatus(): Promise<NativeSidecarStatus> {
  return api<NativeSidecarStatus>("/api/native/status");
}

const NATIVE_START_FETCH_TIMEOUT_MS = 150_000;

export function startNativeStream(sessionId: string, context: unknown, gameTitle?: string): Promise<NativeSidecarStatus> {
  const controller = new AbortController();
  const timer = window.setTimeout(() => controller.abort(), NATIVE_START_FETCH_TIMEOUT_MS);
  const body: Record<string, unknown> = { sessionId, context };
  if (gameTitle?.trim()) {
    // Spread gameTitle into context so finalizeNativeContext can pick it up
    (body.context as Record<string, unknown>) = { ...(context as Record<string, unknown>), gameTitle: gameTitle.trim() };
  }
  return api<NativeSidecarStatus>("/api/native/start", {
    method: "POST",
    body: JSON.stringify(body),
    signal: controller.signal,
  }).finally(() => window.clearTimeout(timer)).catch((error: unknown) => {
    if (error instanceof DOMException && error.name === "AbortError") {
      throw new Error("Native start timed out after 150s without an answer from the app backend.");
    }
    throw error;
  });
}

export function stopNativeStream(): Promise<NativeSidecarStatus> {
  setNativeSurfaceAttached(false);
  return api<NativeSidecarStatus>("/api/native/stop", { method: "POST" });
}

/**
 * True once a native surface update actually carried this window's HWND to the
 * engine, i.e. the engine presents into a child surface clipped inside the app
 * window instead of a window of its own. The stream UI uses it to decide
 * whether React chrome can be drawn on top of the native video plane.
 */
let nativeSurfaceAttached = false;
const nativeSurfaceListeners = new Set<() => void>();

export function isNativeSurfaceAttached(): boolean {
  return nativeSurfaceAttached;
}

/** Re-render hook for components whose layout depends on the embed state. */
export function subscribeNativeSurfaceAttached(listener: () => void): () => void {
  nativeSurfaceListeners.add(listener);
  return () => {
    nativeSurfaceListeners.delete(listener);
  };
}

export function setNativeSurfaceAttached(attached: boolean): void {
  if (nativeSurfaceAttached === attached) return;
  nativeSurfaceAttached = attached;
  for (const listener of [...nativeSurfaceListeners]) listener();
}

/**
 * Adopt the engine's verdict once it reports one. `undefined` (no report yet)
 * keeps the optimistic value written when the handle was sent — the engine only
 * logs after it processed the command, so silence means "in flight", not
 * "failed".
 */
export function syncNativeSurfaceAttached(reported: boolean | undefined): void {
  if (typeof reported === "boolean") setNativeSurfaceAttached(reported);
}

/**
 * Deck actions for a running native session ("recording-toggle",
 * "microphone-toggle", …). The backend keeps a whitelist; unsupported types are
 * rejected with 400.
 */
export function sendNativeCommand(type: string): Promise<NativeSidecarStatus> {
  return api<NativeSidecarStatus>("/api/native/command", {
    method: "POST",
    body: JSON.stringify({ type }),
  });
}

/**
 * Forward browser-side stream diagnostics to the server console so a single
 * server log shows both sides of the signaling/ICE handshake. Fire-and-forget,
 * batched once per second and hard-capped to keep it debug-grade, not noisy.
 */
export function clientLog(line: string): void {
  if (clientLogBuffer.length >= CLIENT_LOG_BATCH_LIMIT) return;
  clientLogBuffer.push(`${new Date().toISOString()} ${line.slice(0, CLIENT_LOG_LINE_LIMIT)}`);
  if (clientLogFlushTimer !== null) return;
  clientLogFlushTimer = window.setTimeout(() => {
    clientLogFlushTimer = null;
    const lines = clientLogBuffer.splice(0, CLIENT_LOG_BATCH_LIMIT);
    if (lines.length === 0) return;
    void api("/api/client-log", { method: "POST", body: JSON.stringify({ lines }) }).catch(() => {
      // Diagnostics delivery is best-effort by design.
    });
  }, 1000);
}

export async function api<T>(path: string, init: RequestInit = {}): Promise<T> {
  const response = await fetch(path, {
    ...init,
    credentials: "same-origin",
    headers: {
      ...(init.body ? { "Content-Type": "application/json" } : {}),
      "X-OpenNOW-Client": "web",
      ...init.headers,
    },
  });
  if (!response.ok) {
    const payload = await response.json().catch(() => null) as {
      error?: string;
      title?: string;
      description?: string;
      gfnErrorCode?: number;
    } | null;
    const error = new Error(payload?.error ?? `Request failed (${response.status})`) as Error & {
      title?: string;
      description?: string;
      gfnErrorCode?: number;
    };
    // Surface GFN session error details so the launch error UI can show the
    // real reason (e.g. "Queue Abandoned") instead of a generic failure.
    if (payload?.title) error.title = payload.title;
    if (payload?.description) error.description = payload.description;
    if (typeof payload?.gfnErrorCode === "number") error.gfnErrorCode = payload.gfnErrorCode;
    throw error;
  }
  if (response.status === 204) return undefined as T;
  return response.json() as Promise<T>;
}

function toAuthSession(session: BrowserSession): AuthSession {
  return {
    provider: session.provider,
    user: session.user,
    tokens: {
      accessToken: "server-managed",
      idToken: "server-managed",
      expiresAt: Number.MAX_SAFE_INTEGER,
    },
  };
}

function readSettings(): Settings {
  try {
    const raw = localStorage.getItem("opennow.web.settings");
    const stored = raw ? JSON.parse(raw) as Partial<Settings> : {};
    const settings: Settings = {
      ...WEB_DEFAULT_SETTINGS,
      ...stored,
      showNativeStreamerStats: false,
      nativeExternalRenderer: false,
    };
    // One-time provision of the live stats HUD (GFN-style overlay). Existing
    // sessions predate the default-on change; respect explicit toggles made
    // after provisioning.
    if (settings.statsHudProvisioned !== true) {
      settings.showStatsOnLaunch = true;
      settings.statsHudProvisioned = true;
      writeSettings(settings);
    }
    // Builds before the stream-mode setting persisted a mode the user never
    // chose, so until a mode is picked explicitly the shipped default owns it.
    if (settings.streamModeChosen !== true) {
      settings.streamClientMode = WEB_DEFAULT_SETTINGS.streamClientMode;
      settings.transportMode = WEB_DEFAULT_SETTINGS.transportMode;
      writeSettings(settings);
    }
    return settings;
  } catch {
    return { ...WEB_DEFAULT_SETTINGS };
  }
}

function writeSettings(settings: Settings): void {
  localStorage.setItem("opennow.web.settings", JSON.stringify(settings));
}

type SignalingListener = (event: MainToRendererSignalingEvent) => void;
let socket: WebSocket | null = null;
/** Passive socket that carries engine events during a native (NVST) session. */
let nativeChannelSocket: WebSocket | null = null;
let nativeChannelWanted = false;
let nativeChannelRetryTimer: number | null = null;
const signalingListeners = new Set<SignalingListener>();

function emitSignaling(event: MainToRendererSignalingEvent): void {
  for (const listener of signalingListeners) listener(event);
}

function sendSignal(type: string, payload?: unknown): Promise<void> {
  if (!socket || socket.readyState !== WebSocket.OPEN) {
    return Promise.reject(new Error("The signaling bridge is not connected."));
  }
  socket.send(JSON.stringify({ type, ...(payload === undefined ? {} : { payload }) }));
  return Promise.resolve();
}

function routeSignalingFrame(data: unknown): void {
  try {
    const parsed = JSON.parse(String(data)) as { type?: string; payload?: MainToRendererSignalingEvent };
    if (parsed.type === "event" && parsed.payload) emitSignaling(parsed.payload);
  } catch {
    emitSignaling({ type: "error", message: "Received an invalid signaling message." });
  }
}

/**
 * Whether a native (NVST) stream is running right now. The API layer cannot
 * read React state, so the app publishes it here; without it the deck's
 * fullscreen button would keep calling the shell command, which a maximised
 * window answers by staying windowed.
 */
let nativeEngineRunning = false;

export function setNativeEngineRunning(running: boolean): void {
  nativeEngineRunning = running;
}

function nativeEngineRunningForFullscreen(): boolean {
  return nativeEngineRunning;
}

async function connectSignaling(payload: SignalingConnectRequest): Promise<void> {
  socket?.close();
  // The native event channel is a passive socket on the same endpoint; the
  // WebRTC connect supersedes it (its frames are routed identically).
  closeNativeEventChannel();
  const protocol = location.protocol === "https:" ? "wss:" : "ws:";
  socket = new WebSocket(`${protocol}//${location.host}/api/signaling`);
  await new Promise<void>((resolve, reject) => {
    const activeSocket = socket;
    if (!activeSocket) return reject(new Error("Unable to create signaling connection."));
    activeSocket.onopen = () => {
      activeSocket.send(JSON.stringify({ type: "connect", payload: { ...payload, nativeStreamer: undefined } }));
      resolve();
    };
    activeSocket.onerror = () => reject(new Error("Unable to open the signaling bridge."));
    activeSocket.onmessage = (message) => routeSignalingFrame(message.data);
    activeSocket.onclose = (event) => emitSignaling({ type: "disconnected", reason: event.reason || "bridge closed" });
  });
}

/**
 * Native (NVST) sessions get every engine-initiated event — Ctrl+G/Ctrl+N/F11
 * chords, clipboard-paste requests, live counters — over the same
 * `/api/signaling` socket the WebRTC player uses. A native session never opens
 * that socket (there is no SDP to negotiate), so without this the engine's
 * events have nowhere to go and the deck's shortcuts only worked when the
 * WebView itself happened to see the key — which it does not for the chords
 * WebView2 treats as browser accelerators (Ctrl+N, F11). Open a passive socket
 * (no `connect` frame) for the lifetime of the native session.
 */
function openNativeEventChannel(): void {
  nativeChannelWanted = true;
  if (nativeChannelSocket && nativeChannelSocket.readyState <= WebSocket.OPEN) return;
  if (socket && socket.readyState === WebSocket.OPEN) return;
  const protocol = location.protocol === "https:" ? "wss:" : "ws:";
  const opened = new WebSocket(`${protocol}//${location.host}/api/signaling`);
  nativeChannelSocket = opened;
  opened.onmessage = (message) => routeSignalingFrame(message.data);
  opened.onerror = () => {
    // `onclose` follows and drives the retry.
  };
  opened.onclose = () => {
    if (nativeChannelSocket === opened) nativeChannelSocket = null;
    if (!nativeChannelWanted || nativeChannelRetryTimer !== null) return;
    // The backend may still be coming up (or restarted): keep trying while the
    // native session is alive so a shortcut never silently goes deaf.
    nativeChannelRetryTimer = window.setTimeout(() => {
      nativeChannelRetryTimer = null;
      if (nativeChannelWanted) openNativeEventChannel();
    }, 1500);
  };
}

function closeNativeEventChannel(): void {
  nativeChannelWanted = false;
  if (nativeChannelRetryTimer !== null) {
    window.clearTimeout(nativeChannelRetryTimer);
    nativeChannelRetryTimer = null;
  }
  const opened = nativeChannelSocket;
  nativeChannelSocket = null;
  if (!opened) return;
  opened.onclose = null;
  opened.onerror = null;
  opened.onmessage = null;
  try {
    opened.close();
  } catch {
    // Already closing.
  }
}

const screenshots: ScreenshotEntry[] = [];
const recordings: RecordingEntry[] = [];
const recordingChunks = new Map<string, { mimeType: string; chunks: ArrayBuffer[] }>();
const updaterState = {
  status: "disabled" as const,
  currentVersion: "0.5.1-web",
  currentDisplayVersion: "0.5.1 Web",
  updateSource: "github-releases" as const,
  canCheck: false,
  canDownload: false,
  canInstall: false,
  isPackaged: true,
  message: "The web app is updated by its host.",
};

const REGION_PING_CONCURRENCY = 6;
const REGION_PING_TIMEOUT_MS = 6_000;
/**
 * Probes per region. The first request on a cold connection pays DNS + TCP +
 * TLS setup, which inflates the measurement to several round trips (a ~30ms
 * link reports ~175ms). Warm the connection up, discard that sample, then
 * time keep-alive requests — each reuses the established connection and
 * measures roughly one network round trip.
 */
const REGION_PING_WARMUP_PROBES = 1;
const REGION_PING_SAMPLE_PROBES = 3;

async function probeRegionLatencyOnce(url: string, timeoutMs: number): Promise<number> {
  const controller = new AbortController();
  const timeout = window.setTimeout(() => controller.abort(), timeoutMs);

  try {
    const target = new URL(url);
    if (target.protocol !== "https:") {
      throw new Error("Region latency checks require HTTPS.");
    }

    const startedAt = performance.now();
    await fetch(target.toString(), {
      method: "GET",
      mode: "no-cors",
      cache: "no-store",
      credentials: "omit",
      redirect: "follow",
      referrerPolicy: "no-referrer",
      signal: controller.signal,
    });

    return performance.now() - startedAt;
  } finally {
    window.clearTimeout(timeout);
  }
}

async function measureRegionLatency(url: string): Promise<PingResult> {
  const samples: number[] = [];
  let lastError: unknown = null;

  for (let probe = 0; probe < REGION_PING_WARMUP_PROBES + REGION_PING_SAMPLE_PROBES; probe += 1) {
    try {
      samples.push(await probeRegionLatencyOnce(url, REGION_PING_TIMEOUT_MS));
    } catch (error) {
      lastError = error;
    }
  }

  if (samples.length === 0) {
    return {
      url,
      pingMs: null,
      error: lastError instanceof Error ? lastError.message : "Region latency check failed.",
    };
  }

  // Drop the cold-connection sample whenever at least one keep-alive
  // sample succeeded, and report the fastest probe (jitter only adds time).
  const timedSamples = samples.length > REGION_PING_WARMUP_PROBES
    ? samples.slice(REGION_PING_WARMUP_PROBES)
    : samples;
  const bestSample = Math.min(...timedSamples);

  return {
    url,
    pingMs: Math.max(1, Math.round(bestSample)),
  };
}

async function pingRegionsInBrowser(regions: Parameters<OpenNowApi["pingRegions"]>[0]): Promise<PingResult[]> {
  const results = new Array<PingResult>(regions.length);
  let nextIndex = 0;

  const worker = async (): Promise<void> => {
    while (nextIndex < regions.length) {
      const index = nextIndex;
      nextIndex += 1;
      results[index] = await measureRegionLatency(regions[index].url);
    }
  };

  await Promise.all(
    Array.from(
      { length: Math.min(REGION_PING_CONCURRENCY, regions.length) },
      () => worker(),
    ),
  );

  return results;
}

const bridge: OpenNowApi = {
  getAuthSession: async () => {
    const result = await api<{ session: BrowserSession | null }>("/api/session");
    return {
      session: result.session ? toAuthSession(result.session) : null,
      refresh: { attempted: false, forced: false, outcome: "not_attempted", message: "Server-managed web session." },
    };
  },
  getLoginProviders: () => api("/api/providers"),
  getRegions: () => api("/api/regions"),
  login: async () => { throw new Error("OpenNOW Web uses link-based sign in."); },
  startDeviceLogin: async (input) => {
    const challenge = await api<DeviceLoginChallengePayload>("/api/auth/device/start", { method: "POST", body: JSON.stringify(input) });
    return { ...challenge, deviceCode: challenge.attemptId } satisfies AuthDeviceLoginChallenge;
  },
  pollDeviceLogin: async (input) => {
    const result = await api<DeviceLoginPollPayload>("/api/auth/device/poll", { method: "POST", body: JSON.stringify({ attemptId: input.attemptId }) });
    return { ...result, session: result.session ? toAuthSession(result.session) : undefined };
  },
  completeDeviceLogin: async () => {
    const result = await api<{ session: BrowserSession | null }>("/api/session");
    if (!result.session) throw new Error("Sign-in did not complete.");
    return toAuthSession(result.session);
  },
  cancelDeviceLogin: (input) => api("/api/auth/device/cancel", { method: "POST", body: JSON.stringify(input) }),
  logout: () => api("/api/logout", { method: "POST" }),
  logoutAll: () => api("/api/logout", { method: "POST" }),
  getSavedAccounts: async () => {
    const result = await api<{ session: BrowserSession | null }>("/api/session");
    return result.session ? [{
      userId: result.session.user.userId,
      displayName: result.session.user.displayName,
      email: result.session.user.email,
      avatarUrl: result.session.user.avatarUrl,
      membershipTier: result.session.user.membershipTier,
      providerCode: result.session.provider.code,
    }] : [];
  },
  switchAccount: async () => {
    const result = await api<{ session: BrowserSession | null }>("/api/session");
    if (!result.session) throw new Error("Saved account not found.");
    return toAuthSession(result.session);
  },
  removeAccount: () => api("/api/logout", { method: "POST" }),
  fetchSubscription: () => api("/api/subscription"),
  fetchPersistentStorageLocations: async () => ({ locations: [] }),
  resetPersistentStorage: async (input = {}) => ({ ok: true, storageRegion: input.storageRegion ?? null }),
  fetchGameAccountConnections: async () => ({ accounts: [], fetchedAt: Date.now() }),
  linkGameAccount: async () => { throw new Error("Account linking must be completed on the GeForce NOW website."); },
  unlinkGameAccount: async () => { throw new Error("Account unlinking must be completed on the GeForce NOW website."); },
  resyncGameAccount: async () => { throw new Error("Account syncing must be completed on the GeForce NOW website."); },
  fetchMainGames: async () => (await api<{ games: GameInfo[] }>("/api/catalog")).games,
  fetchStorePanels: async () => {
    const games = (await api<{ games: GameInfo[] }>("/api/catalog")).games;
    return [{ id: "web-catalog", title: "Featured", sections: [{ id: "featured", title: "Featured", games }] }];
  },
  fetchFeaturedGames: async () => (await api<{ games: GameInfo[] }>("/api/catalog")).games.slice(0, 20),
  fetchLibraryGames: async () => (await api<{ games: GameInfo[] }>("/api/library")).games,
  browseCatalog: (input) => api(`/api/catalog${input.searchQuery ? `?q=${encodeURIComponent(input.searchQuery)}` : ""}`),
  fetchPublicGames: () => api("/api/public-games"),
  resolveLaunchAppId: async (input) => (await api<{ appId: string | null }>("/api/resolve-launch-id", { method: "POST", body: JSON.stringify(input) })).appId,
  resolveStoreUrl: async (input) => (await api<{ url: string | null }>("/api/resolve-store-url", { method: "POST", body: JSON.stringify(input) })).url,
  markGameOwned: (input) => api("/api/mark-owned", { method: "POST", body: JSON.stringify(input) }),
  getPendingDirectLaunchRequest: async () => null,
  onDirectLaunchRequest: () => () => {},
  createSession: (input) => api("/api/stream/create", { method: "POST", body: JSON.stringify({ ...input, token: undefined }) }),
  pollSession: (input) => api("/api/stream/poll", { method: "POST", body: JSON.stringify({ ...input, token: undefined }) }),
  reportSessionAd: (input) => api("/api/stream/report-ad", { method: "POST", body: JSON.stringify({ ...input, token: undefined }) }),
  stopSession: (input) => api("/api/stream/stop", { method: "POST", body: JSON.stringify({ ...input, token: undefined }) }),
  getActiveSessions: async () => (await api<{ sessions: Awaited<ReturnType<OpenNowApi["getActiveSessions"]>> }>("/api/active-sessions")).sessions,
  claimSession: (input) => api("/api/stream/claim", { method: "POST", body: JSON.stringify({ ...input, token: undefined }) }),
  getNativeStreamerStatus: async () => createUnsupportedNativeStreamerStatus(),
  getNativeCloudGsyncCapabilities: async () => unsupportedNativeCloudGsyncCapabilities("Browser WebRTC mode"),
  showSessionConflictDialog: async () => "resume",
  connectSignaling,
  disconnectSignaling: async () => { if (socket?.readyState === WebSocket.OPEN) await sendSignal("disconnect"); socket?.close(); socket = null; },
  openNativeEventChannel: () => {
    openNativeEventChannel();
    clientLog("[Native] engine event channel requested");
  },
  closeNativeEventChannel: () => {
    closeNativeEventChannel();
    clientLog("[Native] engine event channel closed");
  },
  sendAnswer: (payload: SendAnswerRequest) => sendSignal("answer", payload),
  sendIceCandidate: (payload: IceCandidatePayload) => sendSignal("ice", payload),
  sendNativeInput: () => {},
  setNativeFullscreen: (fullscreen: boolean) => {
    // The engine owns the window the stream is clipped in, so the deck asks it
    // to fullscreen (frameless, monitor-sized) or restore it. The engine
    // reports the state back over the native event channel.
    void api<NativeSidecarStatus>("/api/native/command", {
      method: "POST",
      body: JSON.stringify({ type: "shell-fullscreen", fullscreen }),
    }).catch((error) => {
      console.warn(`Native fullscreen (${fullscreen ? "enter" : "exit"}) failed:`, error);
    });
  },
  toggleNativePointerLock: () => {
    // F8 / the deck's mouse-lock control: the engine's Raw Input thread owns the
    // pointer in an embedded session, so it is the only side that can hand it
    // over and take it back.
    void api<NativeSidecarStatus>("/api/native/command", {
      method: "POST",
      body: JSON.stringify({ type: "pointer-lock-toggle" }),
    }).catch(() => {});
  },
  setNativeInputPaused: (paused: boolean) => {
    // The engine owns raw input in native sessions: pausing it releases the
    // mouse so the React deck can be clicked, and resuming hands it back.
    // Without a native session the backend answers 409 and nothing happens.
    void api<NativeSidecarStatus>("/api/native/command", {
      method: "POST",
      body: JSON.stringify({ type: "input-paused", paused }),
    }).catch(() => {});
  },
  updateNativeRenderSurface: (() => {
    let cachedHandle: string | null = null;
    let lastRect: { x: number; y: number; width: number; height: number } | null = null;
    let lastVisible = false;
    let lastHandle: string | null = null;
    let pendingHandleFetch = false;
    return (input: { rect: { x: number; y: number; width: number; height: number } | null; visible: boolean; deviceScaleFactor: number; showStats?: boolean; windowHandle?: string; screenRect?: { x: number; y: number; width: number; height: number } | null }) => {
      const tauri = (window as any).__TAURI__ as { core?: { invoke?: (cmd: string, args?: any) => Promise<any> } } | undefined;
      const invoke = tauri?.core?.invoke?.bind(tauri.core);
      // The shell's breadcrumb (written to the app data dir on launch) is the
      // fallback for resolving this window's HWND: the Tauri IPC bridge is only
      // available when a capability grants the loopback origin, and without a
      // handle the engine opens a window of its own.
      let shellHandleFetch: Promise<string | undefined> | null = null;
      const fetchShellHandle = (): Promise<string | undefined> => {
        if (!shellHandleFetch) {
          shellHandleFetch = api<{ handle?: string | null }>("/api/native/surface-handle")
            .then((result) => {
              const handle =
                typeof result?.handle === "string" && result.handle !== "0" ? result.handle : undefined;
              // The shell may not have published yet (backend boots first):
              // forget the miss so the next publish re-asks.
              if (!handle) shellHandleFetch = null;
              return handle;
            })
            .catch(() => {
              shellHandleFetch = null;
              return undefined;
            });
        }
        return shellHandleFetch;
      };
      const doSend = (handle?: string) => {
        const rectKey = input.rect ? `${input.rect.x},${input.rect.y},${input.rect.width},${input.rect.height}` : "null";
        const visibleKey = input.visible;
        // Don't spam identical rect without handle, but always send if we have handle
        const handleKey = handle ?? input.windowHandle ?? cachedHandle ?? null;
        if (
          lastRect &&
          `${lastRect.x},${lastRect.y},${lastRect.width},${lastRect.height}` === rectKey &&
          lastVisible === visibleKey &&
          lastHandle === handleKey
        ) {
          return;
        }
        lastRect = input.rect ? { ...input.rect } : null;
        lastVisible = visibleKey;
        lastHandle = handleKey;
        const finalHandle = handle || input.windowHandle || cachedHandle || undefined;
        // If visible and no handle, we must still try to get handle — don't send without handle for visible=true
        // because backend errors "missing Qt window handle" and shows black screen
        if (input.visible && !finalHandle) {
          // Trigger handle fetch and retry
          if (invoke && !pendingHandleFetch) {
            pendingHandleFetch = true;
            void invoke("get_window_handle").then((h: string) => {
              pendingHandleFetch = false;
              if (h && h !== "0") {
                cachedHandle = h;
                doSend(h);
              }
            }).catch(() => {
              pendingHandleFetch = false;
            });
          }
          // Don't send visible=true without handle yet — wait for handle
          return;
        }
        const body = {
          rect: input.rect,
          visible: input.visible,
          deviceScaleFactor: input.deviceScaleFactor,
          showStats: input.showStats ?? false,
          windowHandle: finalHandle,
          screenRect: input.screenRect || input.rect,
        };
        if (input.visible && finalHandle) {
          setNativeSurfaceAttached(true);
        }
        void api("/api/native/surface", { method: "POST", body: JSON.stringify(body) }).catch(() => {});
      };
      if (invoke) {
        if (cachedHandle) {
          doSend(cachedHandle);
        } else if (!pendingHandleFetch) {
          pendingHandleFetch = true;
          void invoke("get_window_handle").then((h: string) => {
            pendingHandleFetch = false;
            if (h && h !== "0") {
              cachedHandle = h;
              doSend(h);
              return;
            }
            void fetchShellHandle().then((shellHandle) => {
              if (shellHandle) cachedHandle = shellHandle;
              doSend(shellHandle);
            });
          }).catch(() => {
            pendingHandleFetch = false;
            void fetchShellHandle().then((shellHandle) => {
              if (shellHandle) cachedHandle = shellHandle;
              doSend(shellHandle);
            });
          });
        } else {
          // Handle fetch pending, but if we have input.windowHandle, send it
          if (input.windowHandle) doSend(input.windowHandle);
        }
      } else {
        // No Tauri bridge in this page (browser session, or IPC not granted for
        // the loopback origin). Ask the backend for the shell's breadcrumb.
        void fetchShellHandle().then((shellHandle) => {
          doSend(input.windowHandle || shellHandle || undefined);
        });
      }
    };
  })(),
  updateNativeShortcuts: () => {},
  requestKeyframe: (payload: KeyframeRequest) => sendSignal("keyframe", payload),
  onSignalingEvent: (listener) => { signalingListeners.add(listener); return () => signalingListeners.delete(listener); },
  onToggleFullscreen: () => () => {},
  onExitFullscreen: () => () => {},
  quitApp: async () => { location.assign("/"); },
  getUpdaterState: async () => updaterState,
  checkForUpdates: async () => updaterState,
  downloadUpdate: async () => updaterState,
  installUpdateAndRestart: async () => updaterState,
  onUpdaterStateChanged: () => () => {},
  /**
   * Fullscreen for the current session. A native (NVST) session routes this to
   * the engine, which owns the shell window the plane is clipped in and can
   * make it frameless and monitor-sized — something the shell's own
   * `set_app_fullscreen` (and a maximise) cannot do.
   */
  setFullscreen: async (value) => {
    if (nativeEngineRunningForFullscreen()) {
      window.openNow?.setNativeFullscreen?.(value);
      return;
    }
    // The desktop shell owns a real window and the native video plane is a
    // child window positioned inside it, so a DOM fullscreen request alone
    // never grows the OS window. Ask the shell first, then run the DOM path as
    // the fallback for browsers and shells without that command.
    const tauriCore = (window as unknown as {
      __TAURI__?: { core?: { invoke?: (command: string, args?: Record<string, unknown>) => Promise<unknown> } };
    }).__TAURI__?.core;
    const tauriInvoke = typeof tauriCore?.invoke === "function" ? tauriCore.invoke.bind(tauriCore) : undefined;
    if (tauriInvoke) {
      try {
        await tauriInvoke("set_app_fullscreen", { fullscreen: value });
        clientLog(`[Native] shell window fullscreen ${value ? "on" : "off"}`);
        // The OS window (and with it the embedded video plane) is what had to
        // grow, and it just did. The in-page fullscreen API would only
        // fullscreen the WebView inside that window — and it is the call that
        // can reject, which used to abort the state update below.
        return;
      } catch (error) {
        clientLog(`[Native] shell window fullscreen failed (${value ? "enter" : "exit"}): ${String(error)}`);
        console.warn(`Native window fullscreen failed (${value ? "enter" : "exit"}):`, error);
      }
    }
    if (value && !document.fullscreenElement) await document.documentElement.requestFullscreen();
    else if (!value && document.fullscreenElement) await document.exitFullscreen();
  },
  toggleFullscreen: async () => { if (document.fullscreenElement) await document.exitFullscreen(); else await document.documentElement.requestFullscreen(); },
  togglePointerLock: async () => { if (document.pointerLockElement) document.exitPointerLock(); else await document.documentElement.requestPointerLock(); },
  notifyPointerLockChange: () => {},
  readClipboardText: () => navigator.clipboard.readText(),
  getSettings: async () => readSettings(),
  setSetting: async (key, value) => { const settings = readSettings(); settings[key] = value; writeSettings(settings); },
  resetSettings: async () => { const settings = { ...WEB_DEFAULT_SETTINGS }; writeSettings(settings); return settings; },
  selectNativeStreamerExecutable: async () => null,
  getMicrophonePermission: async () => ({ platform: "linux", isMacOs: false, status: "not-applicable", granted: false, canRequest: true, shouldUseBrowserApi: true }),
  exportLogs: async () => "OpenNOW Web logs are available in the browser developer console.",
  pingRegions: pingRegionsInBrowser,
  saveScreenshot: async (input) => {
    const id = crypto.randomUUID();
    const entry: ScreenshotEntry = { id, fileName: `${input.gameTitle ?? "OpenNOW"}-${Date.now()}.png`, filePath: id, createdAtMs: Date.now(), sizeBytes: input.dataUrl.length, dataUrl: input.dataUrl };
    screenshots.unshift(entry);
    return entry;
  },
  listScreenshots: async () => screenshots,
  deleteScreenshot: async (input) => { const index = screenshots.findIndex((item) => item.id === input.id); if (index >= 0) screenshots.splice(index, 1); },
  saveScreenshotAs: async (input) => { const item = screenshots.find((entry) => entry.id === input.id); if (!item) return { saved: false }; const anchor = document.createElement("a"); anchor.href = item.dataUrl; anchor.download = item.fileName; anchor.click(); return { saved: true, filePath: item.fileName }; },
  onTriggerScreenshot: () => () => {},
  onExternalEscape: () => () => {},
  openExternalUrl: async (url) => { const parsed = new URL(url); if (!["http:", "https:"].includes(parsed.protocol)) throw new Error("Unsupported external URL."); window.open(parsed.toString(), "_blank", "noopener,noreferrer"); },
  beginRecording: async (input) => { const recordingId = crypto.randomUUID(); recordingChunks.set(recordingId, { mimeType: input.mimeType, chunks: [] }); return { recordingId }; },
  sendRecordingChunk: async (input) => { recordingChunks.get(input.recordingId)?.chunks.push(input.chunk); },
  finishRecording: async (input) => { const pending = recordingChunks.get(input.recordingId); if (!pending) throw new Error("Recording not found."); const blob = new Blob(pending.chunks, { type: pending.mimeType }); const entry: RecordingEntry = { id: input.recordingId, fileName: `${input.gameTitle ?? "OpenNOW"}-${Date.now()}.webm`, filePath: URL.createObjectURL(blob), createdAtMs: Date.now(), sizeBytes: blob.size, durationMs: input.durationMs, gameTitle: input.gameTitle, thumbnailDataUrl: input.thumbnailDataUrl }; recordings.unshift(entry); recordingChunks.delete(input.recordingId); return entry; },
  abortRecording: async (input) => { recordingChunks.delete(input.recordingId); },
  listRecordings: async () => recordings,
  deleteRecording: async (input) => { const index = recordings.findIndex((item) => item.id === input.id); if (index >= 0) { URL.revokeObjectURL(recordings[index].filePath); recordings.splice(index, 1); } },
  showRecordingInFolder: async () => {},
  listMediaByGame: async (input = {}) => ({ screenshots: screenshots.filter((item) => !input.gameTitle || item.fileName.includes(input.gameTitle)), videos: recordings.filter((item) => !input.gameTitle || item.gameTitle === input.gameTitle) }),
  getMediaThumbnail: async (input) => screenshots.find((item) => item.filePath === input.filePath)?.dataUrl ?? recordings.find((item) => item.filePath === input.filePath)?.thumbnailDataUrl ?? null,
  showMediaInFolder: async () => {},
  getMediaPlaybackUrl: async (input) => recordings.find((item) => item.filePath === input.filePath)?.filePath ?? null,
  deleteMediaFile: async (input) => { const index = recordings.findIndex((item) => item.filePath === input.filePath); if (index >= 0) recordings.splice(index, 1); return { ok: index >= 0 }; },
  regenMediaThumbnail: async (input) => ({ ok: true, thumbnailDataUrl: recordings.find((item) => item.filePath === input.filePath)?.thumbnailDataUrl ?? null }),
  deleteCache: async () => { localStorage.removeItem("opennow.catalog.snapshot.v1"); },
  fetchPrintedWasteQueue: async () => ({}),
  fetchPrintedWasteServerMapping: async () => ({}),
  getThanksData: async () => ({ contributors: [], supporters: [], contributorsError: "Community data is unavailable in this web build." }),
  provisionZortosCommunityProxy: async () => { throw new Error("Community proxy provisioning is unavailable in the hosted web app."); },
  setDiscordActivity: async () => {},
  clearDiscordActivity: async () => {},
  getReleaseHighlights: async (version = "0.5.1-web") => ({ version, title: `OpenNOW ${version}`, bodyMarkdown: "OpenNOW is now available in your browser.", source: "fallback" }),
  ackReleaseHighlights: async () => {},
  onReleaseHighlightsShow: () => () => {},
};

/**
 * Playtime ledger (server-backed, stored in the installation's app-data
 * directory). The browser keeps a cache of the last summary so the Playtime
 * page paints instantly; these calls are the source of truth.
 */
export function fetchPlaytimeSummary(): Promise<PlaytimeSummary> {
  return api<PlaytimeSummary>("/api/playtime");
}

export function recordPlaytimeSession(
  payload: PlaytimeSessionPayload,
  options: { keepalive?: boolean } = {},
): Promise<PlaytimeSummary> {
  return api<PlaytimeSummary>("/api/playtime/session", {
    method: "POST",
    body: JSON.stringify(payload),
    ...(options.keepalive ? { keepalive: true } : {}),
  });
}

export function importLegacyPlaytime(records: LegacyPlaytimeRecord[]): Promise<PlaytimeSummary> {
  return api<PlaytimeSummary>("/api/playtime/import", {
    method: "POST",
    body: JSON.stringify({ records } satisfies PlaytimeImportPayload),
  });
}

export function resetPlaytimeLedger(): Promise<PlaytimeSummary> {
  return api<PlaytimeSummary>("/api/playtime", { method: "DELETE" });
}

export function installBrowserBridge(): void {
  window.openNow = bridge;
}
