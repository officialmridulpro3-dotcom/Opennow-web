import type { Server as HttpServer, IncomingMessage } from "node:http";
import type { Duplex } from "node:stream";
import { WebSocketServer, WebSocket } from "ws";

import type { IceCandidatePayload, KeyframeRequest, SendAnswerRequest, SignalingConnectRequest } from "@shared/gfn";
import { GfnSignalingClient } from "./gfn/signaling";
import { getExistingSession } from "./sessionStore";
import { onNativeEvent } from "./nativeStream";

type ClientMessage =
  | { type: "connect"; payload: SignalingConnectRequest }
  | { type: "answer"; payload: SendAnswerRequest }
  | { type: "ice"; payload: IceCandidatePayload }
  | { type: "keyframe"; payload: KeyframeRequest }
  | { type: "disconnect" };

function originAllowed(request: IncomingMessage): boolean {
  const origin = request.headers.origin;
  if (!origin) return process.env.NODE_ENV !== "production";
  if (process.env.OPENNOW_ORIGIN) return origin === process.env.OPENNOW_ORIGIN;
  const host = request.headers.host;
  return origin === `http://${host}` || origin === `https://${host}`;
}

export function attachSignalingBridge(server: HttpServer): void {
  const wss = new WebSocketServer({ noServer: true, maxPayload: 2 * 1024 * 1024 });

  server.on("upgrade", (request, socket: Duplex, head) => {
    const url = new URL(request.url ?? "/", `http://${request.headers.host ?? "localhost"}`);
    if (url.pathname !== "/api/signaling" || !originAllowed(request)) {
      socket.destroy();
      return;
    }
    wss.handleUpgrade(request, socket, head, (ws) => wss.emit("connection", ws, request));
  });

  wss.on("connection", (socket: WebSocket, request) => {
    const browserSession = getExistingSession(request as never);
    if (!browserSession) {
      socket.close(4401, "Authentication required");
      return;
    }

    let signaling: GfnSignalingClient | null = null;
    let unsubscribe: (() => void) | null = null;
    const send = (payload: unknown) => {
      if (socket.readyState === WebSocket.OPEN) socket.send(JSON.stringify(payload));
    };

    // Forward native runtime status and supported shortcut/fullscreen events to the web client; overlay requests are filtered by the sidecar supervisor.
    const unsubscribeNative = onNativeEvent((event) => {
      send({ type: "event", payload: event });
    });

    socket.on("message", (raw) => {
      void (async () => {
        const message = JSON.parse(raw.toString("utf8")) as ClientMessage;
        if (message.type === "connect") {
          const input = message.payload;
          if (!browserSession.ownsActiveSession(input.sessionId)) {
            throw new Error("Stream ownership check failed.");
          }
          signaling?.disconnect();
          unsubscribe?.();
          signaling = new GfnSignalingClient(input.signalingServer, input.sessionId, input.signalingUrl);
          unsubscribe = signaling.onEvent((event) => send({ type: "event", payload: event }));
          await signaling.connect();
        } else if (message.type === "answer") {
          console.log(`[SignalingBridge] Relaying browser answer (${message.payload.sdp.length} chars) to the game server.`);
          await signaling?.sendAnswer(message.payload);
        } else if (message.type === "ice") {
          console.log(`[SignalingBridge] Relaying browser ICE candidate: ${String(message.payload.candidate).slice(0, 80)}`);
          if (!signaling) {
            console.warn("[SignalingBridge] Dropped browser ICE candidate: signaling is not connected yet.");
          }
          await signaling?.sendIceCandidate(message.payload);
        } else if (message.type === "keyframe") {
          await signaling?.requestKeyframe(message.payload);
        } else if (message.type === "disconnect") {
          signaling?.disconnect();
        }
      })().catch((error) => send({ type: "event", payload: { type: "error", message: error instanceof Error ? error.message : String(error) } }));
    });

    socket.on("close", () => {
      unsubscribe?.();
      unsubscribeNative();
      signaling?.disconnect();
    });
  });
}
