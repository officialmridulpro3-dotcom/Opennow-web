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
import { existsSync } from "node:fs";

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
}

export interface FinalizedNativeContext {
  sessionId: string;
  context: NativeStreamerSessionContext;
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
 */
export function buildSidecarEnv(): NodeJS.ProcessEnv {
  return {
    ...process.env,
    OPENNOW_NATIVE_EXTERNAL_RENDERER: "1",
  };
}

/**
 * Validate the client-built session context and force the settings the NVST
 * engine requires. The client's shared builder already maps SessionInfo and
 * stream settings; the transport override lives here so web defaults (which
 * normalize to webrtc) can never leak into a native launch.
 */
export function finalizeNativeContext(input: unknown): FinalizedNativeContext {
  const context = input as Partial<NativeStreamerSessionContext> | null;
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
  const finalized = {
    ...(context as Record<string, unknown>),
    session: { ...(session as Record<string, unknown>), extra: { ...extra, rtspsEndpoints: endpoints } },
    settings: { ...settings, transportMode: "nvst", nativeVideoBackend: "auto" },
    shortcuts,
  } as unknown as NativeStreamerSessionContext;
  return { sessionId, context: finalized };
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
    };
  }

  async start(sessionId: string, context: NativeStreamerSessionContext): Promise<NativeSidecarStatus> {
    if (this.child) {
      throw httpError("A native stream is already running — stop it first.", 409);
    }
    const exe = sidecarPath();
    if (!exe) throw httpError("Native streaming is only available in the desktop app.", 501);

    this.exitCode = undefined;
    this.lastError = undefined;
    this.capabilities = undefined;
    this.stdoutBuffer = "";

    const child = spawn(exe, [], { stdio: ["pipe", "pipe", "pipe"], windowsHide: true, env: buildSidecarEnv() });
    this.child = child;
    this.sessionId = sessionId;
    console.log(`[NVST] spawned sidecar pid=${child.pid} session=${sessionId}`);

    child.stdout?.on("data", (chunk: Buffer) => this.onStdout(chunk));
    child.stderr?.on("data", (chunk: Buffer) => {
      const text = chunk.toString("utf8").trim();
      if (text) console.log(`[NVST:${child.pid}] ${text.slice(0, 2000)}`);
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
    if (type === "error") {
      const detail = typeof message.message === "string" ? message.message : "native streamer error";
      this.lastError = detail.slice(0, 500);
      console.log(`[NVST] engine error: ${this.lastError}`);
      this.pending?.readyReject(new Error(detail));
      this.pending = null;
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
    this.exitCode = code;
    if (error && !this.lastError) this.lastError = error;
    console.log(`[NVST] sidecar exited code=${code}${error ? ` (${error})` : ""}`);
    this.pending?.readyReject(new Error(error ?? "Native sidecar exited during startup."));
    this.pending = null;
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
