/**
 * Native (NVST) sidecar supervision.
 *
 * The desktop shell bundles upstream's native streaming engine
 * (third_party/opennow-streamer) as the `opennow-nvst` sidecar and passes its
 * path via OPENNOW_NVST_SIDECAR. This module spawns one sidecar per stream,
 * drives its JSON-lines protocol (hello -> start -> ... -> shutdown), and
 * reports its state. The engine opens its own D3D11 window and captures input
 * itself, so no rendering or input bridging is needed.
 *
 * Lifecycle notes:
 * - stdin EOF makes the engine shut itself down (its host loop exits when the
 *   protocol thread ends), so a backend crash does not orphan the sidecar.
 * - The shell kills the backend on app exit, which cascades to the sidecar.
 * - Only one sidecar runs at a time; starting again 409s until stopped.
 */
import { spawn, type ChildProcess } from "node:child_process";
import { lookup as dnsLookup } from "node:dns/promises";
import { existsSync, mkdirSync } from "node:fs";
import { isIP } from "node:net";
import { join, resolve } from "node:path";

import type { NativeStreamerSessionContext } from "@shared/gfn";

const ENGINE_PROTOCOL_VERSION = 7;
const SHUTDOWN_GRACE_MS = 5000;

export interface NativeSidecarStatus {
  /** False on web deployments without a bundled sidecar. */
  supported: boolean;
  running: boolean;
  pid?: number;
  sessionId?: string;
  exitCode?: number | null;
  lastError?: string;
  capabilities?: unknown;
  /** Startup phase while `running` but not yet acknowledged ("handshake" | "starting"). */
  phase?: "handshake" | "starting";
  /** True once the engine logs its first inbound video datagram or decoded frame. */
  firstFrame?: boolean;
  /** Active engine-side MKV recording (Ctrl+G menu / F12 toggle), if any. */
  recording?: { path: string; startedAtMs: number };
  /**
   * Whether the engine presents into a child surface clipped inside the app
   * window. `undefined` until the engine reports; `false` means it fell back to
   * a window of its own.
   */
  surfaceAttached?: boolean;
  /** Engine log marker explaining a `surfaceAttached: false` verdict. */
  surfaceError?: string;
}

interface ActiveNativeRecording {
  path: string;
  startedAtMs: number;
  startCommandId: string;
}

export interface FinalizedNativeContext {
  sessionId: string;
  context: NativeStreamerSessionContext;
  gameTitle?: string;
}

function sidecarPath(): string | null {
  const raw = process.env.OPENNOW_NVST_SIDECAR?.trim();
  if (!raw) return null;
  return existsSync(raw) ? raw : null;
}

/**
 * Environment for the sidecar process. The engine only opens its own visible
 * game window when OPENNOW_NATIVE_EXTERNAL_RENDERER=1 — otherwise it renders
 * to a hidden 2x2 surface (embedded/Qt-host mode) and native launches show
 * nothing. We have no Qt host, so external is the only working mode: force on.
 * Input capture is likewise engine-side: OPENNOW_NATIVE_INPUT_OWNER=native
 * arms the sidecar's SDL + Raw Input capture (keyboard, mouse, gamepad).
 * Without it the game window renders but ignores all input.
 */
export function buildSidecarEnv(gameTitle?: string): NodeJS.ProcessEnv {
  // FIX black screen: Windows embedded in-app requires external_renderer=true
  // When false, sidecar uses hidden 2x2 window and no video shows (black).
  // When true, WindowsExternalSdlSurface is created and can operate as:
  // - embedded child (when surface command with Tauri HWND arrives) → in-app
  // - standalone top-level (when no surface command) → separate window
  // We always want true, and we send surface commands to make it embedded.
  // User wants no WebRTC black screen, in-app native with GFN sidebar.
  return {
    ...process.env,
    OPENNOW_NATIVE_EXTERNAL_RENDERER: "1",
    OPENNOW_NATIVE_INPUT_OWNER: "native",
    // The shell owns the window layout: the engine must never reveal a
    // top-level window of its own, it waits (hidden) for the surface command
    // that carries this window's HWND.
    OPENNOW_NATIVE_SHELL_PLACEMENT: "1",
    // Ctrl+G / Ctrl+N / Guide belong to the styled overlay window instead of
    // the engine's built-in GDI panel, and the engine publishes its live
    // counters as `native-stream-stats` lines for the overlay HUD.
    OPENNOW_NATIVE_HOST_OVERLAY: "1",
    ...(gameTitle?.trim() ? { OPENNOW_GAME_TITLE: gameTitle.trim() } : {}),
  };
}

/**
 * Pull the negotiated stream shape out of a launch context. Tolerant by design:
 * any missing field just leaves the label out of the stats event.
 */
function readStreamProfile(context: NativeStreamerSessionContext): {
  codec?: string;
  resolution?: string;
  requestedFps?: number;
  targetBitrateMbps?: number;
  hardwareAcceleration?: string;
} {
  const settings = (context as unknown as { settings?: Record<string, unknown> }).settings ?? {};
  const asString = (value: unknown): string | undefined =>
    typeof value === "string" && value.trim() ? value.trim() : undefined;
  const asNumber = (value: unknown): number | undefined =>
    typeof value === "number" && Number.isFinite(value) ? value : undefined;
  return {
    codec: asString(settings.codec),
    resolution: asString(settings.resolution),
    requestedFps: asNumber(settings.fps),
    targetBitrateMbps: asNumber(settings.maxBitrateKbps) !== undefined
      ? (asNumber(settings.maxBitrateKbps) as number) / 1000
      : undefined,
    hardwareAcceleration: asString(settings.nativeVideoBackend),
  };
}

/**
 * Validate the client-built session context and force the settings the NVST
 * engine requires. The client's shared builder already maps SessionInfo and
 * stream settings; the transport override lives here so web defaults (which
 * normalize to webrtc) can never leak into a native launch.
 */
export function finalizeNativeContext(input: unknown): FinalizedNativeContext {
  const context = input as (Partial<NativeStreamerSessionContext> & { gameTitle?: unknown }) | null;
  const session = context?.session as unknown as Record<string, unknown> | undefined;
  const sessionId = typeof session?.sessionId === "string" ? session.sessionId.trim() : "";
  const serverIp = typeof session?.serverIp === "string" ? (session.serverIp as string).trim() : "";
  if (!sessionId) throw httpError("Native launch needs a session ID.", 400);
  if (!serverIp) {
    throw httpError("This session has no game-server address yet — wait for the seat to become ready.", 400);
  }
  const extra = (session?.extra as Record<string, unknown> | undefined) ?? {};
  // rtspsEndpoints may arrive flattened in extra (engine shape) or top-level
  // (our SessionInfo shape) — accept both, forward as extra.
  const rawEndpoints = extra.rtspsEndpoints ?? (session as Record<string, unknown>)?.rtspsEndpoints;
  const endpoints = Array.isArray(rawEndpoints) ? rawEndpoints.filter((e): e is string => typeof e === "string") : [];
  if (!endpoints.some((e) => e.startsWith("rtsps://") || e.startsWith("rtsp://"))) {
    throw httpError("CloudMatch did not provide an RTSPS endpoint for this session.", 400);
  }
  const settings = ((context?.settings ?? {}) as Record<string, unknown>);
  const shortcuts = ((context?.shortcuts ?? {}) as Record<string, unknown>);
  if (typeof settings !== "object" || typeof shortcuts !== "object") {
    throw httpError("Native launch needs stream settings and shortcut bindings.", 400);
  }
  const gameTitle = typeof context?.gameTitle === "string" ? context.gameTitle.trim() : undefined;
  const finalized = {
    ...(context as Record<string, unknown>),
    session: { ...(session as Record<string, unknown>), extra: { ...extra, rtspsEndpoints: endpoints } },
    settings: { ...settings, transportMode: "nvst", nativeVideoBackend: "auto" },
    shortcuts,
  } as unknown as NativeStreamerSessionContext;
  return { sessionId, context: finalized, gameTitle: gameTitle || undefined };
}

/**
 * CloudMatch media peers arrive as zone-LB hostnames (e.g.
 * `80-250-98-39.cloudmatchbeta.nvidiagrid.net`), but the NVST engine opens
 * its UDP bundle socket against a literal IP — passing the hostname through
 * makes `start` fail with "media peer is not an IP address". Resolve once
 * here. The rtsps:// endpoint URLs and serverIp keep their hostnames
 * (TLS/SNI needs them) — only the media peer IP is rewritten.
 */
export async function resolveNativeMediaPeer(
  context: NativeStreamerSessionContext,
  lookup: (host: string) => Promise<string> = defaultLookupIpv4,
): Promise<NativeStreamerSessionContext> {
  const media = context.session.mediaConnectionInfo;
  const host = media?.ip?.trim() ?? "";
  if (!media || !host || isIP(host) !== 0) return context;
  let ip: string;
  try {
    ip = await lookup(host);
  } catch (error) {
    throw httpError(
      `Couldn't resolve the game server address "${host}" — check your connection, DNS, or VPN, then retry. (${(error as Error).message})`,
      502,
    );
  }
  console.log(`[NVST] resolved media peer ${host} -> ${ip}`);
  return {
    ...context,
    session: { ...context.session, mediaConnectionInfo: { ...media, ip } },
  };
}

async function defaultLookupIpv4(host: string): Promise<string> {
  return (await dnsLookup(host, { family: 4 })).address;
}

/**
 * Resolve the transport CloudMatch should provision. A native (NVST) request
 * is honored only when a sidecar binary is present to play it — web
 * deployments and sidecar-less shells always fall back to WebRTC.
 */
export function resolveLaunchTransportMode(requested: unknown): {
  clientMode: "native" | "web";
  transportMode: "nvst" | "webrtc";
} {
  if (requested === "nvst" && sidecarPath() !== null) {
    return { clientMode: "native", transportMode: "nvst" };
  }
  return { clientMode: "web", transportMode: "webrtc" };
}

function httpError(message: string, statusCode: number): Error & { statusCode: number } {
  return Object.assign(new Error(message), { statusCode });
}

interface PendingStart {
  sessionId: string;
  readyResolve: () => void;
  readyReject: (error: Error) => void;
  startResolved: boolean;
}

/**
 * Sidecar stderr markers proving video is flowing. The engine logs the first
 * raw-SRTP datagram as soon as packets arrive and the first access unit once
 * a complete frame is assembled for the decoder (codec name varies).
 */
const FIRST_FRAME_MARKERS = ["inbound first datagram", "first H264 access unit", "first H265 access unit", "first AV1 access unit"];

/**
 * Engine markers for the in-app surface handshake, in the order they matter.
 *
 * The engine presents into a child window of the shell only after a `surface`
 * command carrying the shell HWND reaches it. When that never happens it falls
 * back to a window of its own — the bug this handshake exists to prevent — so
 * the host watches the engine's stderr for the verdict and reports it to the
 * client instead of assuming the embed worked.
 */
const SURFACE_ATTACHED_MARKER = "External SDL surface attached";
const SURFACE_FAILURE_MARKERS = [
  "External SDL surface attach failed",
  "external SDL surface: visible rect but no window handle",
  "shell-placement: standalone window suppressed",
];


class NativeSidecarManager {
  private child: ChildProcess | null = null;
  private sessionId: string | undefined;
  private exitCode: number | null | undefined;
  private lastError: string | undefined;
  private capabilities: unknown;
  private stdoutBuffer = "";
  private pending: PendingStart | null = null;
  private commandId = 0;
  private phase: "handshake" | "starting" | undefined;
  private firstFrame = false;
  /** `undefined` until the engine reports on the surface handshake. */
  private surfaceAttached: boolean | undefined;
  private surfaceError: string | undefined;
  private recording: ActiveNativeRecording | null = null;
  /**
   * Stream shape taken from the launch context. The engine's once-per-second
   * telemetry line carries rates, not labels, so the negotiated codec and
   * resolution are remembered here and merged into every stats event.
   */
  private streamProfile: {
    codec?: string;
    resolution?: string;
    requestedFps?: number;
    targetBitrateMbps?: number;
    hardwareAcceleration?: string;
  } = {};

  status(): NativeSidecarStatus {
    return {
      supported: sidecarPath() !== null,
      running: this.child !== null,
      pid: this.child?.pid,
      sessionId: this.sessionId,
      exitCode: this.exitCode,
      lastError: this.lastError,
      capabilities: this.capabilities,
      phase: this.child !== null ? this.phase : undefined,
      firstFrame: this.firstFrame || undefined,
      surfaceAttached: this.surfaceAttached,
      surfaceError: this.surfaceError,
      recording: this.recording ? { path: this.recording.path, startedAtMs: this.recording.startedAtMs } : undefined,
    };
  }

  async start(sessionId: string, context: NativeStreamerSessionContext, gameTitle?: string): Promise<NativeSidecarStatus> {
    if (this.child) {
      throw httpError("A native stream is already running — stop it first.", 409);
    }
    const exe = sidecarPath();
    if (!exe) throw httpError("Native streaming is only available in the desktop app.", 501);

    this.exitCode = undefined;
    this.lastError = undefined;
    this.capabilities = undefined;
    this.stdoutBuffer = "";
    this.firstFrame = false;
    this.surfaceAttached = undefined;
    this.surfaceError = undefined;
    this.streamProfile = readStreamProfile(context);

    const child = spawn(exe, [], { stdio: ["pipe", "pipe", "pipe"], windowsHide: true, env: buildSidecarEnv(gameTitle) });
    this.child = child;
    this.sessionId = sessionId;
    console.log(`[NVST] spawned sidecar pid=${child.pid} session=${sessionId}`);

    child.stdout?.on("data", (chunk: Buffer) => this.onStdout(chunk));
    child.stderr?.on("data", (chunk: Buffer) => {
      const text = chunk.toString("utf8").trim();
      if (!text) return;
      // The sidecar renders in its own OS window, so the web client never
      // sees a video element frame. Surface the engine's first-video markers
      // so the launch overlay can dismiss on the real first clear frame.
      if (!this.firstFrame && FIRST_FRAME_MARKERS.some((marker) => text.includes(marker))) {
        this.firstFrame = true;
        console.log(`[NVST:${child.pid}] first video frame observed`);
      }
      if (text.includes(SURFACE_ATTACHED_MARKER)) {
        this.surfaceAttached = true;
        this.surfaceError = undefined;
        console.log(`[NVST:${child.pid}] in-app surface attached to the shell window`);
      } else {
        const failure = SURFACE_FAILURE_MARKERS.find((marker) => text.includes(marker));
        if (failure) {
          this.surfaceAttached = false;
          this.surfaceError = failure;
          console.log(`[NVST:${child.pid}] in-app surface NOT attached: ${failure}`);
        }
      }
      console.log(`[NVST:${child.pid}] ${text.slice(0, 2000)}`);
    });
    child.on("error", (error) => this.onExit(null, `sidecar process error: ${error.message}`));
    child.on("exit", (code) => this.onExit(code, code === 0 ? undefined : `sidecar exited with code ${code}`));

    const started = new Promise<void>((resolve, reject) => {
      this.pending = { sessionId, readyResolve: resolve, readyReject: reject, startResolved: false };
    });
    // hello -> ready initializes the engine; start begins NVST negotiation.
    // Await the start acknowledgement so callers get a synchronous error when
    // negotiation input is rejected (missing endpoint, bad profile, ...).
    // Both waits are bounded: a silent sidecar must surface as an error,
    // never an infinite spinner.
    this.phase = "handshake";
    this.send({ id: this.nextId("hello"), type: "hello", protocolVersion: ENGINE_PROTOCOL_VERSION });
    try {
      await this.awaitHandshake(started, "hello");
    } catch (error) {
      await this.stop("handshake failed");
      throw error;
    }
    const startId = this.nextId("start");
    const acknowledged = new Promise<void>((resolve, reject) => {
      this.pending = { sessionId, readyResolve: resolve, readyReject: reject, startResolved: true };
    });
    this.phase = "starting";
    this.send({ id: startId, type: "start", context });
    try {
      await this.awaitHandshake(acknowledged, "start");
    } catch (error) {
      await this.stop("start rejected");
      throw error;
    }
    this.phase = undefined;
    return this.status();
  }

  /**
   * Bound a handshake wait. Budgets are tunable via
   * OPENNOW_NVST_{HELLO,START}_TIMEOUT_MS (milliseconds).
   */
  private awaitHandshake(promise: Promise<void>, kind: "hello" | "start"): Promise<void> {
    const fallbackMs = kind === "hello" ? 30_000 : 120_000;
    const requested = Number(
      kind === "hello" ? process.env.OPENNOW_NVST_HELLO_TIMEOUT_MS : process.env.OPENNOW_NVST_START_TIMEOUT_MS,
    );
    const ms = Number.isFinite(requested) && requested > 0 ? requested : fallbackMs;
    let timer: ReturnType<typeof setTimeout> | undefined;
    const timeout = new Promise<void>((_resolve, reject) => {
      timer = setTimeout(() => {
        const what = kind === "hello" ? "answer the startup handshake" : "acknowledge stream start";
        this.lastError =
          `Native sidecar did not ${what} within ${Math.round(ms / 1000)}s — ` +
          `it may be stuck probing this GPU or reaching the game server. ` +
          `Its last lines are in server.log ([NVST]).`;
        console.log(`[NVST] ${this.lastError}`);
        this.pending = null;
        try {
          this.child?.kill();
        } catch {
          // Already gone.
        }
        reject(new Error(this.lastError));
      }, ms);
    });
    return Promise.race([promise, timeout]).finally(() => clearTimeout(timer));
  }

  async stop(reason = "stopped"): Promise<NativeSidecarStatus> {
    const child = this.child;
    if (!child) return this.status();
    this.pending?.readyReject(new Error("Native stream stopped."));
    this.pending = null;
    if (this.recording) {
      // The engine finalizes the MKV worker during shutdown; drop the local
      // handle so a later session never reports a stale recording.
      console.log(`[NVST] recording discarded with session stop (${reason}): ${this.recording.path}`);
      this.recording = null;
    }
    try {
      this.send({ id: this.nextId("shutdown"), type: "shutdown", reason });
    } catch {
      // Pipe already broken — fall through to kill.
    }
    child.stdin?.end();
    const exited = new Promise<void>((resolve) => {
      if (this.child !== child) {
        resolve();
        return;
      }
      const timer = setTimeout(() => {
        try {
          child.kill();
        } catch {
          // Already gone.
        }
        resolve();
      }, SHUTDOWN_GRACE_MS);
      child.once("exit", () => {
        clearTimeout(timer);
        resolve();
      });
    });
    await exited;
    return this.status();
  }

  updateSurface(surface: {
    rect: { x: number; y: number; width: number; height: number } | null;
    visible: boolean;
    deviceScaleFactor: number;
    showStats?: boolean;
    windowHandle?: string;
    screenRect?: { x: number; y: number; width: number; height: number } | null;
  }): void {
    if (!this.child) return;
    try {
      // Protocol expects surface command with window_handle as string (HWND)
      // and rect in physical pixels. This drives WindowsExternalSdlSurface::update
      // which attaches SDL child window to Tauri parent.
      console.log(
        `[NVST] surface rect ${surface.rect ? `${surface.rect.x},${surface.rect.y} ${surface.rect.width}x${surface.rect.height}` : "none"} visible=${surface.visible} handle=${surface.windowHandle ?? "-"}`,
      );
      this.send({
        id: this.nextId("surface"),
        type: "surface",
        surface: {
          rect: surface.rect,
          visible: surface.visible,
          deviceScaleFactor: surface.deviceScaleFactor,
          showStats: surface.showStats ?? false,
          windowHandle: surface.windowHandle,
          screenRect: surface.screenRect ?? surface.rect,
        },
      });
    } catch (error) {
      console.log(`[NVST] surface update failed: ${(error as Error).message}`);
    }
  }

  private nextId(prefix: string): string {
    this.commandId += 1;
    return `${prefix}-${this.commandId}`;
  }

  private send(message: Record<string, unknown>): void {
    if (!this.child?.stdin || this.child.stdin.destroyed) {
      throw new Error("Native sidecar input is closed.");
    }
    this.child.stdin.write(`${JSON.stringify(message)}\n`);
  }

  /**
   * Whitelisted engine commands the styled overlay deck may trigger. The list
   * is deliberately small — anything that changes transport, session or capture
   * shape stays with the engine, the shell or the launch flow.
   */
  command(type: string, options: { paused?: boolean } = {}): NativeSidecarStatus {
    if (!this.child) throw httpError("No native stream is running.", 409);
    switch (type) {
      case "recording-toggle":
        this.toggleRecording();
        break;
      case "recording-start":
        this.startRecording();
        break;
      case "recording-stop":
        this.stopRecording("deck");
        break;
      case "microphone-toggle":
      case "fullscreen-toggle":
        this.send({ id: this.nextId(type), type });
        break;
      case "input-paused":
        // Engine-side capture toggle: the styled deck releases the mouse while
        // it is open and hands it back when it closes.
        this.send({ id: this.nextId(type), type, paused: options.paused === true });
        break;
      default:
        throw httpError(`Unsupported native command: ${type || "(empty)"}`, 400);
    }
    return this.status();
  }

  private toggleRecording(): void {
    if (!this.child) {
      console.log("[NVST] toggle-recording ignored: the sidecar is not running");
      return;
    }
    if (this.recording) {
      this.stopRecording("toggle");
      return;
    }
    this.startRecording();
  }

  private startRecording(): void {
    const sessionId = this.sessionId ?? "session";
    const dir = resolve(process.env.OPENNOW_DATA_DIR?.trim() || ".opennow-data", "recordings");
    try {
      mkdirSync(dir, { recursive: true });
    } catch (error) {
      console.log(`[NVST] recording start failed: cannot create ${dir} (${(error as Error).message})`);
      return;
    }
    const safeSession = sessionId.replace(/[^a-zA-Z0-9_-]+/g, "_").slice(0, 64) || "session";
    const path = join(dir, `${safeSession}-${Date.now()}.mkv`);
    const id = this.nextId("recording-start");
    try {
      // Command shape follows the protocol crate (camelCase): outputPath must
      // be absolute with an .mkv extension or the engine rejects the start.
      this.send({ id, type: "recording-start", outputPath: path });
    } catch (error) {
      console.log(`[NVST] recording start failed: ${(error as Error).message}`);
      return;
    }
    this.recording = { path, startedAtMs: Date.now(), startCommandId: id };
    console.log(`[NVST] recording started -> ${path}`);
  }

  private stopRecording(reason: string): void {
    const active = this.recording;
    this.recording = null;
    if (!active || !this.child) return;
    try {
      this.send({ id: this.nextId("recording-stop"), type: "recording-stop" });
    } catch (error) {
      console.log(`[NVST] recording stop (${reason}) send failed: ${(error as Error).message}`);
      return;
    }
    console.log(`[NVST] recording stopping (${reason}): ${active.path}`);
  }

  private onStdout(chunk: Buffer): void {
    this.stdoutBuffer += chunk.toString("utf8");
    const lines = this.stdoutBuffer.split("\n");
    this.stdoutBuffer = lines.pop() ?? "";
    for (const line of lines) {
      const trimmed = line.trim();
      if (!trimmed) continue;
      let message: Record<string, unknown>;
      try {
        message = JSON.parse(trimmed) as Record<string, unknown>;
      } catch {
        continue;
      }
      this.onMessage(message);
    }
  }

  private onMessage(message: Record<string, unknown>): void {
    const type = typeof message.type === "string" ? message.type : "";
    if (type === "ready" && message.capabilities !== undefined) {
      this.capabilities = message.capabilities;
    }
    // The engine publishes `telemetry` once a second (frames per second, bitrate,
    // ping, jitter, packet loss). Re-shape it as the client's native stats event
    // so the diagnostics store and the styled overlay HUD both show live engine
    // numbers instead of the same three frozen values.
    if (type === "telemetry") {
      const numberOf = (key: string): number | undefined => {
        const value = message[key];
        return typeof value === "number" && Number.isFinite(value) ? value : undefined;
      };
      const bitrateMbps = numberOf("bitrateMbps") ?? 0;
      const framesPerSecond = numberOf("framesPerSecond") ?? 0;
      const targetMbps = this.streamProfile.targetBitrateMbps ?? 0;
      emitNativeEvent({
        type: "native-stream-stats",
        stats: {
          codec: this.streamProfile.codec ?? "h264",
          resolution: this.streamProfile.resolution ?? "",
          hardwareAcceleration: this.streamProfile.hardwareAcceleration ?? "native decode",
          bitrateKbps: Math.round(bitrateMbps * 1000),
          targetBitrateKbps: Math.round(targetMbps * 1000),
          bitratePerformancePercent: targetMbps > 0 ? Math.min(100, (bitrateMbps / targetMbps) * 100) : 0,
          decodedFps: framesPerSecond,
          renderFps: framesPerSecond,
          framesDecoded: numberOf("framesDecoded") ?? 0,
          framesRendered: numberOf("framesDecoded") ?? 0,
          sinkDropped: numberOf("framesDropped") ?? 0,
          rttMs: numberOf("pingMs"),
          packetLossPercent: numberOf("packetLossPercent") ?? 0,
          zeroCopyD3D11: false,
          zeroCopyD3D12: false,
        },
      });
      return;
    }
    if (type === "error") {
      const detail = typeof message.message === "string" ? message.message : "native streamer error";
      this.lastError = detail.slice(0, 500);
      console.log(`[NVST] engine error: ${this.lastError}`);
      if (typeof message.id === "string" && this.recording?.startCommandId === message.id) {
        console.log(`[NVST] recording failed to start: ${this.lastError}`);
        this.recording = null;
      }
      this.pending?.readyReject(new Error(detail));
      this.pending = null;
      return;
    }
    // Engine-initiated session controls. Ctrl+Shift+Q (stop-stream) quits the
    // native session from the keyboard. Ctrl+G (Guide) in embedded mode now
    // opens the React full sidebar (opaque left, fully functional) instead of
    // the old GDI box. Ctrl+N toggles compact stats (340x520 GeForce style).
    if (type === "overlay-request") {
      console.log("[NVST] engine overlay-request (embedded host menu request) -> React sidebar");
      emitNativeEvent({ type: "native-shortcut", action: "toggleSidebar" });
      return;
    }
    if (type === "shortcut-action" && message.action === "stop-stream") {
      console.log("[NVST] engine shortcut stop-stream; stopping native session");
      void this.stop("shortcut stop-stream");
      return;
    }
    if (type === "shortcut-action" && message.action === "toggle-stats") {
      console.log("[NVST] engine shortcut toggle-stats (embedded host stats request)");
      emitNativeEvent({ type: "native-shortcut", action: "toggleStats" });
      return;
    }
    // F11 / Alt+Enter and the mouse-lock binding are handled by the engine while
    // it owns the keyboard (the game has the focus, not the web page), so the
    // host has to apply them to the app window — otherwise fullscreen would be
    // unreachable exactly when a game is running.
    if (type === "shortcut-action" && message.action === "toggle-fullscreen") {
      console.log("[NVST] engine shortcut toggle-fullscreen; toggling the app window");
      emitNativeEvent({ type: "native-shortcut", action: "toggleFullscreen" });
      return;
    }
    if (type === "shortcut-action" && message.action === "toggle-pointer-lock") {
      console.log("[NVST] engine shortcut toggle-pointer-lock");
      emitNativeEvent({ type: "native-shortcut", action: "togglePointerLock" });
      return;
    }
    // F12 / Ctrl+G "Recording" row: the engine only reports the toggle — the
    // MKV worker itself is driven through recording-start/stop commands here
    // so clips land next to the other desktop data with a stable file name.
    if (type === "shortcut-action" && message.action === "toggle-recording") {
      console.log("[NVST] engine shortcut toggle-recording; toggling native MKV recording");
      this.toggleRecording();
      return;
    }
    if (type === "recording-started") {
      console.log(`[NVST] recording acknowledged: ${typeof message.path === "string" ? message.path : "(unknown path)"}`);
      return;
    }
    if (type === "recording-stopped") {
      const path = typeof message.path === "string" ? message.path : "(unknown path)";
      console.log(`[NVST] recording saved: ${path} (video ${String(message.videoPackets ?? "?")} packets, audio ${String(message.audioPackets ?? "?")} packets)`);
      return;
    }
    if (type === "recording-not-active") {
      console.log("[NVST] recording stop ignored: no recording was active");
      this.recording = null;
      return;
    }
    if (type === "recording-state") {
      const state = typeof message.state === "string" ? message.state : "unknown";
      const path = typeof message.path === "string" ? message.path : "";
      console.log(`[NVST] recording worker ${state}${path ? `: ${path}` : ""}`);
      if (state === "failed" && this.recording && (!path || this.recording.path === path)) {
        this.recording = null;
      }
      return;
    }
    // Any non-error reply to the in-flight command resolves it; the engine
    // answers start with ok/status lines before streaming events follow.
    if (this.pending && (type === "ready" || type === "ok" || type === "started" || type === "status")) {
      const pending = this.pending;
      this.pending = null;
      pending.readyResolve();
    }
  }

  private onExit(code: number | null, error?: string): void {
    if (!this.child) return;
    this.child = null;
    if (this.recording) {
      console.log(`[NVST] recording discarded with sidecar exit: ${this.recording.path}`);
      this.recording = null;
    }
    this.exitCode = code;
    if (error && !this.lastError) this.lastError = error;
    console.log(`[NVST] sidecar exited code=${code}${error ? ` (${error})` : ""}`);
    this.pending?.readyReject(new Error(error ?? "Native sidecar exited during startup."));
    this.pending = null;
  }
}

type NativeEventListener = (event: Record<string, unknown>) => void;
const nativeEventListeners = new Set<NativeEventListener>();

export function onNativeEvent(listener: NativeEventListener): () => void {
  nativeEventListeners.add(listener);
  return () => nativeEventListeners.delete(listener);
}

function emitNativeEvent(event: Record<string, unknown>): void {
  for (const listener of nativeEventListeners) {
    try {
      listener(event);
    } catch {
      // best-effort
    }
  }
}

export const nativeSidecar = new NativeSidecarManager();

// Belt and suspenders: the engine also exits on stdin EOF, but an explicit
// kill covers interpreters that keep pipes open across exec boundaries.
process.once("exit", () => {
  try {
    (nativeSidecar as unknown as { child: ChildProcess | null }).child?.kill();
  } catch {
    // Best-effort only.
  }
});
