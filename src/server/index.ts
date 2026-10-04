import { createServer } from "node:http";
import { resolve } from "node:path";
import express from "express";

import { registerApi } from "./api";
import { attachSignalingBridge } from "./signalingBridge";
import { cacheManager } from "./services/cacheManager";
import { cookieSessionMiddleware } from "./sessionStore";
import { startDesktopWatchdog } from "./desktopWatchdog";

const app = express();
const server = createServer(app);
const port = Number(process.env.PORT ?? 3000);

app.disable("x-powered-by");
app.use(express.json({ limit: "1mb" }));
app.use("/api", cookieSessionMiddleware);
app.use((request, response, next) => {
  response.setHeader("Referrer-Policy", "same-origin");
  response.setHeader("X-Content-Type-Options", "nosniff");
  response.setHeader("X-Frame-Options", "DENY");
  response.setHeader("Permissions-Policy", "microphone=(self), fullscreen=(self), camera=()");
  if (process.env.NODE_ENV === "production") {
    response.setHeader(
      "Content-Security-Policy",
      "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline' https://fonts.googleapis.com; font-src https://fonts.gstatic.com; img-src 'self' data: https:; connect-src 'self' https: ws: wss:; media-src 'self' blob:; object-src 'none'; base-uri 'self'; frame-ancestors 'none'; form-action 'self'",
    );
  }
  if (request.path.startsWith("/api/")) response.setHeader("Cache-Control", "no-store");
  next();
});
app.use((request, _response, next) => {
  if (request.method !== "GET" && request.path.startsWith("/api/") && request.header("x-opennow-client") !== "web") {
    next(Object.assign(new Error("Invalid same-origin request."), { statusCode: 403 }));
    return;
  }
  next();
});

registerApi(app);
attachSignalingBridge(server);

if (process.env.NODE_ENV === "production") {
  // OPENNOW_STATIC_DIR lets the desktop shell point at the bundled dist copy;
  // regular deployments keep using ./dist next to the server process.
  const staticDir = resolve(process.env.OPENNOW_STATIC_DIR ?? "dist");
  app.use(express.static(staticDir, { index: false, maxAge: "1h" }));
  app.get("/{*path}", (_request, response) => response.sendFile(resolve(staticDir, "index.html")));
} else {
  const { createServer: createViteServer } = await import("vite");
  // Vite rejects unknown Host headers by default. Hosted sandboxes and preview
  // tunnels hand the app a *.e2b.app hostname, so that suffix is allowed out of
  // the box; set OPENNOW_DEV_ALLOWED_HOSTS to a comma-separated list for any
  // other tunnel, cloud IDE, or reverse proxy.
  const devAllowedHosts = [
    ".e2b.app",
    ...(process.env.OPENNOW_DEV_ALLOWED_HOSTS ?? "")
      .split(",")
      .map((host) => host.trim())
      .filter((host) => host.length > 0),
  ];
  const vite = await createViteServer({
    server: {
      middlewareMode: true,
      allowedHosts: devAllowedHosts,
    },
    appType: "spa",
  });
  app.use(vite.middlewares);
}

app.use((error: unknown, _request: express.Request, response: express.Response, _next: express.NextFunction) => {
  // GFN SessionError.statusCode carries the CloudMatch app-level code (0-254),
  // not an HTTP status — e.g. statusCode 69 ("Queue Abandoned") crashed this
  // handler with "RangeError: Invalid status code", so the client never got a
  // usable error at all. Only trust values in the real HTTP range and surface
  // the GFN error details in the JSON body instead.
  const rawStatus = typeof error === "object" && error && "statusCode" in error && typeof error.statusCode === "number"
    ? error.statusCode
    : NaN;
  const detail = (typeof error === "object" && error ? error : {}) as {
    gfnErrorCode?: unknown;
    title?: unknown;
    description?: unknown;
  };
  const isUpstreamGfnError = typeof detail.gfnErrorCode === "number";
  const status = Number.isFinite(rawStatus) && rawStatus >= 100 && rawStatus <= 599
    ? rawStatus
    : isUpstreamGfnError ? 502 : 500;
  const message = error instanceof Error ? error.message : "Unexpected server error.";
  const body: Record<string, unknown> = { error: message };
  if (typeof detail.title === "string") body.title = detail.title;
  if (typeof detail.description === "string") body.description = detail.description;
  if (isUpstreamGfnError) body.gfnErrorCode = detail.gfnErrorCode;
  if (status >= 500) console.error("[Server]", error);
  response.status(status).json(body);
});

await cacheManager.initialize();
server.listen(port, () => {
  console.log(`OpenNOW Web is running at http://localhost:${port}`);
});
