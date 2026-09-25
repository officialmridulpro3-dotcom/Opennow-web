import type { Express, NextFunction, Request, Response } from "express";

import type { CatalogBrowseRequest, SessionAdReportRequest, SessionClaimRequest, SessionCreateRequest, SessionPollRequest, SessionStopRequest } from "@shared/gfn";
import { browseCatalogUncached } from "./gfn/catalogBrowse";
import { resolveLaunchAppId, resolveStoreUrl } from "./gfn/gameAppMapper";
import { fetchLibraryGamesUncached, markGameOwned } from "./gfn/libraryGames";
import { fetchPublicGamesUncached } from "./gfn/publicGames";
import { claimSession, createSession, getActiveSessions, pollSession, reportSessionAd, stopSession } from "./gfn/cloudmatch";
import { resolveClientStreamingBaseUrl } from "./gfn/cloudmatchTransport";
import { fetchSubscription } from "./gfn/subscription";
import { getLoginProviders } from "./webAuth";
import { getSession } from "./sessionStore";
import { finalizeNativeContext, nativeSidecar, resolveLaunchTransportMode, resolveNativeMediaPeer } from "./nativeStream";
import { formatUdpPreflight, runUdpPreflight } from "./udpPreflight";

function asyncRoute(handler: (request: Request, response: Response) => Promise<void>) {
  return (request: Request, response: Response, next: NextFunction) => {
    void handler(request, response).catch(next);
  };
}

export function registerApi(app: Express): void {
  app.get("/api/health", (_request, response) => {
    response.json({ ok: true, runtime: "web", streamer: "webrtc" });
  });

  // Browser-side stream diagnostics pipe. The client batches [WebRTC] /
  // signaling / recovery lines here so one server log shows both sides of the
  // ICE handshake. Session-scoped and hard-capped to keep it debug-grade.
  app.post("/api/client-log", asyncRoute(async (request, response) => {
    const state = getSession(request, response);
    if (!state.publicSession()) {
      throw Object.assign(new Error("Authentication required."), { statusCode: 401 });
    }
    const lines = Array.isArray(request.body?.lines) ? request.body.lines : [];
    for (const line of lines.slice(0, 200)) {
      if (typeof line === "string" && line.trim().length > 0) {
        console.log(`[Client] ${line.trim().slice(0, 2000)}`);
      }
    }
    response.status(204).end();
  }));

  app.get("/api/providers", asyncRoute(async (_request, response) => {
    response.json(await getLoginProviders());
  }));

  app.get("/api/session", asyncRoute(async (request, response) => {
    response.setHeader("Cache-Control", "no-store");
    response.json({ session: getSession(request, response).publicSession() });
  }));

  app.post("/api/auth/device/start", asyncRoute(async (request, response) => {
    const challenge = await getSession(request, response).startDeviceLogin(request.body?.providerIdpId);
    response.json(challenge);
  }));

  app.post("/api/auth/device/poll", asyncRoute(async (request, response) => {
    const attemptId = String(request.body?.attemptId ?? "");
    if (!attemptId) throw Object.assign(new Error("Missing sign-in attempt."), { statusCode: 400 });
    response.json(await getSession(request, response).pollDeviceLogin(attemptId));
  }));

  app.post("/api/auth/device/cancel", asyncRoute(async (request, response) => {
    getSession(request, response).cancelDeviceLogin(String(request.body?.attemptId ?? ""));
    response.status(204).end();
  }));

  app.post("/api/logout", asyncRoute(async (request, response) => {
    getSession(request, response).logout();
    response.status(204).end();
  }));

  app.get("/api/bootstrap", asyncRoute(async (request, response) => {
    const state = getSession(request, response);
    const auth = await state.requireAuth();
    const token = auth.tokens.idToken ?? auth.tokens.accessToken;
    const { regions, vpcId } = await state.regions();
    const subscription = await fetchSubscription(token, auth.user.userId, vpcId ?? undefined).catch(() => null);
    response.json({ session: state.publicSession(), regions, subscription });
  }));

  app.get("/api/regions", asyncRoute(async (request, response) => {
    response.json((await getSession(request, response).regions()).regions);
  }));

  app.get("/api/subscription", asyncRoute(async (request, response) => {
    const state = getSession(request, response);
    const auth = await state.requireAuth();
    const token = auth.tokens.idToken ?? auth.tokens.accessToken;
    const { vpcId } = await state.regions();
    response.json(await fetchSubscription(token, auth.user.userId, vpcId ?? undefined));
  }));

  app.get("/api/library", asyncRoute(async (request, response) => {
    const state = getSession(request, response);
    const auth = await state.requireAuth();
    const token = auth.tokens.idToken ?? auth.tokens.accessToken;
    const games = await fetchLibraryGamesUncached(token, auth.provider.streamingServiceUrl);
    response.json({ games });
  }));

  app.get("/api/catalog", asyncRoute(async (request, response) => {
    const state = getSession(request, response);
    const auth = await state.requireAuth();
    const input: CatalogBrowseRequest = {
      token: auth.tokens.idToken ?? auth.tokens.accessToken,
      providerStreamingBaseUrl: auth.provider.streamingServiceUrl,
      searchQuery: typeof request.query.q === "string" ? request.query.q : undefined,
      fetchCount: 100,
    };
    response.json(await browseCatalogUncached(input));
  }));

  app.get("/api/public-games", asyncRoute(async (_request, response) => {
    response.json(await fetchPublicGamesUncached());
  }));

  app.post("/api/resolve-launch-id", asyncRoute(async (request, response) => {
    const state = getSession(request, response);
    const auth = await state.requireAuth();
    const appId = await resolveLaunchAppId(
      auth.tokens.idToken ?? auth.tokens.accessToken,
      String(request.body?.appIdOrUuid ?? ""),
      auth.provider.streamingServiceUrl,
    );
    response.json({ appId });
  }));

  app.post("/api/resolve-store-url", asyncRoute(async (request, response) => {
    const state = getSession(request, response);
    const auth = await state.requireAuth();
    const url = await resolveStoreUrl(
      auth.tokens.idToken ?? auth.tokens.accessToken,
      String(request.body?.appIdOrUuid ?? ""),
      auth.provider.streamingServiceUrl,
      {
        variantId: request.body?.variantId,
        store: request.body?.store,
      },
    );
    response.json({ url });
  }));

  app.post("/api/mark-owned", asyncRoute(async (request, response) => {
    const state = getSession(request, response);
    const auth = await state.requireAuth();
    const token = auth.tokens.idToken ?? auth.tokens.accessToken;
    const result = await markGameOwned({
      token,
      userId: auth.user.userId,
      variantId: String(request.body?.variantId ?? ""),
      providerStreamingBaseUrl: auth.provider.streamingServiceUrl,
      tokens: [auth.tokens.idToken, auth.tokens.accessToken],
    });
    response.json(result);
  }));

  app.get("/api/active-sessions", asyncRoute(async (request, response) => {
    const state = getSession(request, response);
    const auth = await state.requireAuth();
    const active = await getActiveSessions(
      auth.tokens.idToken ?? auth.tokens.accessToken,
      auth.provider.streamingServiceUrl,
    );
    for (const item of active) state.addActiveSession(item.sessionId);
    response.json({ sessions: active });
  }));

  app.post("/api/stream/create", asyncRoute(async (request, response) => {
    const state = getSession(request, response);
    const auth = await state.requireAuth();
    const requestedId = String(request.body?.appId ?? "");
    const appId = await resolveLaunchAppId(
      auth.tokens.idToken ?? auth.tokens.accessToken,
      requestedId,
      auth.provider.streamingServiceUrl,
    );
    if (!appId) throw Object.assign(new Error("This game does not expose a launchable GFN app ID."), { statusCode: 400 });

    const input = request.body as Omit<SessionCreateRequest, "token" | "appId" | "streamingBaseUrl">;
    const session = await createSession({
      ...input,
      appId,
      token: auth.tokens.idToken ?? auth.tokens.accessToken,
      // Honor the zone the client selected (queue server selector or region
      // setting) when it targets an NVIDIA GRID host. Forcing the provider
      // default here routed every launch to the deployment's local region —
      // e.g. picking Bulgaria while queued still created the session in India.
      streamingBaseUrl: resolveClientStreamingBaseUrl(
        request.body?.streamingBaseUrl,
        auth.provider.streamingServiceUrl,
      ),
      internalTitle: String(input.internalTitle || appId),
      settings: { ...input.settings, ...resolveLaunchTransportMode(input.settings.transportMode) },
    });
    state.addActiveSession(session.sessionId);
    response.json(session);
  }));

  app.post("/api/stream/poll", asyncRoute(async (request, response) => {
    const state = getSession(request, response);
    const input = request.body as SessionPollRequest;
    if (!state.ownsActiveSession(input.sessionId)) {
      throw Object.assign(new Error("This stream does not belong to the current browser session."), { statusCode: 403 });
    }
    const auth = await state.requireAuth();
    const session = await pollSession({
      ...input,
      token: auth.tokens.idToken ?? auth.tokens.accessToken,
      streamingBaseUrl: input.streamingBaseUrl ?? auth.provider.streamingServiceUrl,
    });
    response.json(session);
  }));

  app.post("/api/stream/report-ad", asyncRoute(async (request, response) => {
    const state = getSession(request, response);
    const input = request.body as SessionAdReportRequest;
    if (!state.ownsActiveSession(input.sessionId)) {
      throw Object.assign(new Error("This stream does not belong to the current browser session."), { statusCode: 403 });
    }
    const auth = await state.requireAuth();
    response.json(await reportSessionAd({
      ...input,
      token: auth.tokens.idToken ?? auth.tokens.accessToken,
      streamingBaseUrl: input.streamingBaseUrl ?? auth.provider.streamingServiceUrl,
    }));
  }));

  app.post("/api/stream/claim", asyncRoute(async (request, response) => {
    const state = getSession(request, response);
    const auth = await state.requireAuth();
    const input = request.body as SessionClaimRequest;
    const claimed = await claimSession({
      ...input,
      token: auth.tokens.idToken ?? auth.tokens.accessToken,
      streamingBaseUrl: input.streamingBaseUrl ?? auth.provider.streamingServiceUrl,
      settings: input.settings ? { ...input.settings, ...resolveLaunchTransportMode(input.settings.transportMode) } : undefined,
    });
    state.addActiveSession(claimed.sessionId);
    response.json(claimed);
  }));

  app.post("/api/stream/stop", asyncRoute(async (request, response) => {
    const state = getSession(request, response);
    const input = request.body as SessionStopRequest;
    // Ownership is enforced upstream by the NVIDIA token itself: stopSession
    // can only ever affect sessions that belong to this account. The
    // per-browser ownership check is intentionally skipped so leftover
    // sessions created under an earlier browser session (e.g. after a server
    // restart) can still be cleaned up instead of 403-ing forever.
    const auth = await state.requireAuth();
    await stopSession({
      ...input,
      token: auth.tokens.idToken ?? auth.tokens.accessToken,
      streamingBaseUrl: input.streamingBaseUrl ?? auth.provider.streamingServiceUrl,
    });
    state.removeActiveSession(input.sessionId);
    response.status(204).end();
  }));

  // Native (NVST) sidecar control. The desktop shell bundles the upstream
  // streaming engine and advertises it via OPENNOW_NVST_SIDECAR; web
  // deployments answer `supported: false` and these calls no-op cleanly.
  app.get("/api/native/status", (_request, response) => {
    response.json(nativeSidecar.status());
  });

  app.post("/api/native/start", asyncRoute(async (request, response) => {
    const state = getSession(request, response);
    await state.requireAuth();
    const input = (request.body ?? {}) as { sessionId?: string; context?: unknown };
    if (!input.sessionId || !state.ownsActiveSession(input.sessionId)) {
      response.status(403).json({ error: "This session does not belong to the current browser session." });
      return;
    }
    try {
      const finalized = finalizeNativeContext(input.context);
      if (finalized.sessionId !== input.sessionId) {
        response.status(400).json({ error: "Native context session does not match the requested session." });
        return;
      }
      const context = await resolveNativeMediaPeer(finalized.context);
      // Fire-and-forget UDP preflight: proves whether plain UDP works from
      // this machine. The verdict lands in server.log within seconds and
      // never blocks or fails the launch.
      void runUdpPreflight().then(
        (preflight) => console.log(`[NVST] UDP preflight: ${formatUdpPreflight(preflight)}`),
        (error: unknown) => console.log(`[NVST] UDP preflight error: ${(error as Error).message}`),
      );
      response.json(await nativeSidecar.start(finalized.sessionId, context));
    } catch (error) {
      const statusCode = (error as { statusCode?: number }).statusCode ?? 500;
      response.status(statusCode).json({ error: (error as Error).message });
    }
  }));

  app.post("/api/native/stop", asyncRoute(async (request, response) => {
    const state = getSession(request, response);
    await state.requireAuth();
    response.json(await nativeSidecar.stop());
  }));
}
