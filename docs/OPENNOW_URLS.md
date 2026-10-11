# OpenNOW URL and Endpoint Reference

This file is the single app-level inventory of URL-shaped values that OpenNOW uses. It explains what each one does, where it is used, whether it is contacted by the browser or the server, and gives copyable examples for the main routes.

> **Scope and accuracy.** This reference covers OpenNOW's first-party runtime HTTP/API/WebSocket/stream URLs, remote services and assets, region URL patterns, development/desktop origins, and a short list of project/documentation links. It is based on the source currently in this repository. Some NVIDIA URLs and all game-server/media URLs are discovered at runtime, so their final hostnames cannot be exhaustively hard-coded here. Test fixtures, dependency lockfile registry URLs, generated/retired patch files, and documentation embedded inside bundled third-party codec sources are not product endpoints and are intentionally excluded. Test-only examples such as `https://example.invalid/...` are not listed as live URLs.
>
> **Security rule.** The browser should call OpenNOW's same-origin `/api/...` routes. NVIDIA OAuth tokens and upstream API headers are held by the server and must not be placed in browser code, query strings, documentation, or logs. The server makes the authenticated upstream requests described below.

## Contents

1. [Address and scheme conventions](#1-address-and-scheme-conventions)
2. [OpenNOW same-origin HTTP API](#2-opennow-same-origin-http-api)
3. [OpenNOW signaling WebSocket](#3-opennow-signaling-websocket)
4. [Code examples](#4-code-examples)
5. [NVIDIA / GeForce NOW upstream URLs](#5-nvidia--geforce-now-upstream-urls)
6. [GeForce NOW region hosts](#6-geforce-now-region-hosts)
7. [Streaming, signaling, media, and ICE URLs](#7-streaming-signaling-media-and-ice-urls)
8. [Images, fonts, and external navigation](#8-images-fonts-and-external-navigation)
9. [Proxy URLs and user-supplied URLs](#9-proxy-urls-and-user-supplied-urls)
10. [Development, preview, desktop, and deployment origins](#10-development-preview-desktop-and-deployment-origins)
11. [Project and documentation references](#11-project-and-documentation-references)
12. [Declared or documented URLs that are not live requests](#12-declared-or-documented-urls-that-are-not-live-requests)
13. [Source files to consult](#13-source-files-to-consult)

---

## 1. Address and scheme conventions

| Form | Meaning in OpenNOW |
|---|---|
| `/api/...` | Relative, same-origin HTTP API path. The browser resolves it against the page origin; do not hard-code `localhost` into browser code. |
| `ws(s)://<current-host>/api/signaling` | Same-origin OpenNOW WebSocket. It uses `wss:` when the page is HTTPS and `ws:` when the page is HTTP. |
| `https://<zone>.cloudmatchbeta.nvidiagrid.net/` | NVIDIA GeForce NOW CloudMatch region base URL. Region hosts may be discovered from NVIDIA or selected from the built-in list. |
| `https://<game-server-or-IP>/...` | A per-session CloudMatch control endpoint. The actual game server host is returned by NVIDIA and is not fixed in source. |
| `wss://<signaling-host>/...` | Per-session NVIDIA signaling WebSocket, opened by the server-side signaling client after the browser connects to OpenNOW's own WebSocket. |
| `rtsps://`, `rtsp://`, `stun:`, `turn:`, `turns:` | Streaming control, ICE discovery, and relay schemes; these are not ordinary browser HTTP page URLs. |
| `data:` / `blob:` | Locally generated inline image/media/object URLs. They do not identify a remote server. |

### Request ownership at a glance

- **Browser → OpenNOW:** same-origin `/api/...` routes and `/api/signaling`.
- **OpenNOW server → NVIDIA:** login, subscription, catalog, game ownership, CloudMatch session, public-game, and storage-status requests.
- **Browser → public hosts:** Google Fonts, Steam artwork, optional Gravatar images, and HTTPS-only region latency probes. WebRTC ICE may also use the STUN/TURN services described later.
- **Desktop shell → loopback backend:** its WebView loads `http://127.0.0.1:<port>` (or the dev URL `http://localhost:3000`).

## 2. OpenNOW same-origin HTTP API

All routes below are registered by `src/server/api.ts`; the WebSocket is registered by `src/server/signalingBridge.ts`. Route paths are **relative to the origin currently serving OpenNOW**. For example, if the site is `https://games.example`, `/api/session` resolves to `https://games.example/api/session`.

API responses are marked `Cache-Control: no-store`. The browser API helper also sends the session cookie using `credentials: "same-origin"`. **Every non-GET API request must include `X-OpenNOW-Client: web`**; the standard `src/client/api.ts` helper adds it automatically. JSON request bodies use `Content-Type: application/json`.

### Health, browser session, and device login

| Method | Path | Purpose and behavior |
|---|---|---|
| `GET` | `/api/health` | Liveness check; currently returns `{ ok: true, runtime: "web", streamer: "webrtc" }`. Used by the desktop launcher while waiting for its local backend. |
| `POST` | `/api/client-log` | Best-effort, authenticated browser diagnostics. Accepts `{ "lines": [...] }`; the server caps each batch at 200 lines and each line at 2,000 characters, then returns `204`. |
| `GET` | `/api/providers` | Lists NVIDIA/GeForce NOW login providers. The backend discovers and caches them through NVIDIA's service-URL endpoint; falls back to the NVIDIA provider if discovery fails. |
| `GET` | `/api/session` | Returns `{ session }`, where `session` is the public browser profile/provider or `null`. It does not return OAuth tokens. |
| `POST` | `/api/auth/device/start` | Starts NVIDIA device authorization. Optional body field: `providerIdpId`. Returns an attempt ID, user code, and the verification URL(s) returned by NVIDIA. |
| `POST` | `/api/auth/device/poll` | Polls a device-login attempt. Body: `{ "attemptId": "..." }`. The server exchanges the private device code with NVIDIA and returns a pending/authorized/error result. |
| `POST` | `/api/auth/device/cancel` | Cancels a local pending attempt. Body: `{ "attemptId": "..." }`; returns `204`. |
| `POST` | `/api/logout` | Clears the browser's OpenNOW authentication/session state and returns `204`. |
| `GET` | `/api/bootstrap` | Authenticated initial-load bundle: public session, stream regions, and subscription information. |
| `GET` | `/api/regions` | Authenticated stream-region list. The backend reads NVIDIA CloudMatch `serverInfo` (including VPC metadata internally) and uses a built-in region fallback if discovery fails; this route returns the region array only. |
| `GET` | `/api/subscription` | Authenticated GeForce NOW membership, resolution-entitlement, and subscription summary. |

### Games and catalog

| Method | Path | Purpose and behavior |
|---|---|---|
| `GET` | `/api/library` | Authenticated account library. `?refresh=1` bypasses the normal cache-first path. |
| `GET` | `/api/catalog` | Authenticated game catalog. Optional `q=<encoded text>` searches; `refresh=1` bypasses cached catalog data. |
| `GET` | `/api/public-games` | Publicly supported GeForce NOW game list; no browser login is required for this route. The server fetches NVIDIA's published JSON list. |
| `POST` | `/api/resolve-launch-id` | Resolves an app/UUID into a launchable GFN app ID. Body field: `appIdOrUuid`. Returns `{ appId }`. |
| `POST` | `/api/resolve-store-url` | Resolves a GFN game variant/store destination. Body includes `appIdOrUuid` and may include `variantId` and `store`. Returns `{ url }`; the actual store URL is provider metadata and can vary by game. |
| `POST` | `/api/mark-owned` | Adds/marks a game variant as owned in the NVIDIA library. Authenticated; request includes a variant ID. |

### Cloud gaming sessions

| Method | Path | Purpose and behavior |
|---|---|---|
| `GET` | `/api/active-sessions` | Lists the signed-in account's active GFN sessions and records their IDs in the current browser session. |
| `POST` | `/api/stream/create` | Creates a GFN streaming session. The server resolves the launch app ID, injects the NVIDIA token, validates a requested NVIDIA region base, and sends the upstream CloudMatch request. |
| `POST` | `/api/stream/poll` | Polls a session for queue/setup/ready state and signaling/media information. The session must belong to the current browser session. |
| `POST` | `/api/stream/report-ad` | Reports an ad action for an owned stream session. The body carries the session and ad action/timing data. |
| `POST` | `/api/stream/claim` | Claims/resumes an existing GFN session. The backend adds the resulting session ID to the current browser session. |
| `POST` | `/api/stream/stop` | Stops a GFN session and returns `204`. The server authenticates upstream with the current NVIDIA account token; the NVIDIA API enforces that the session belongs to that account. |

### Native desktop sidecar

| Method | Path | Purpose and behavior |
|---|---|---|
| `GET` | `/api/native/status` | Reports NVST sidecar support/running state. Hosted web deployments normally return `supported: false`; the desktop build may report a bundled sidecar. |
| `POST` | `/api/native/start` | Starts the native streamer for an authenticated active session. Body includes `sessionId` and a native session `context`; the server checks browser-session ownership. |
| `POST` | `/api/native/stop` | Stops the native sidecar for the authenticated desktop session. |
| `POST` | `/api/native/command` | Sends a whitelisted sidecar command. Body includes `type`; fullscreen commands may also include a boolean `fullscreen`. |

Examples of the current client command names include `shell-fullscreen` and `pointer-lock-toggle`. The native game window and its UDP media traffic are separate from the launcher WebView.

### Local playtime ledger

| Method | Path | Purpose and behavior |
|---|---|---|
| `GET` | `/api/playtime` | Reads the local installation's playtime summary. No NVIDIA login is required. |
| `POST` | `/api/playtime/session` | Records one play session. Requires a non-empty `gameId`. |
| `POST` | `/api/playtime/import` | One-time migration endpoint for legacy local playtime data. Reads an array named `records`, limited to 2,000 entries; the store rejects duplicate imports. |
| `DELETE` | `/api/playtime` | Resets the local playtime ledger and returns the new summary. |

## 3. OpenNOW signaling WebSocket

### Local browser-to-OpenNOW bridge

**URL:** `/api/signaling` (WebSocket upgrade)

The browser constructs an absolute URL using the active page origin:

```text
HTTPS page: wss://<same-host>/api/signaling
HTTP page:  ws://<same-host>/api/signaling
```

This is a same-origin bridge, not the NVIDIA game-server WebSocket. It requires an authenticated OpenNOW browser session and checks `Origin`. When `OPENNOW_ORIGIN` is set, the origin must match that configured value exactly. The bridge accepts JSON messages such as `connect`, `answer`, `ice`, `keyframe`, and `disconnect`; server/native events are returned as JSON event messages.

The same endpoint also carries passive native-engine notifications (including fullscreen and pointer-lock state). During native playback it does not perform WebRTC SDP negotiation.

### NVIDIA signaling WebSocket (dynamic per session)

Once a WebRTC `connect` message reaches the server, OpenNOW's server-side client opens the NVIDIA-provided signaling URL. It is built from CloudMatch session response data; the host and path therefore vary per session. Common form:

```text
wss://<signaling-host>/nvst/sign_in?peer_id=<generated-peer>&version=2&peer_role=1&pairing_id=<session-id>
```

If the response supplies a different full `wss://` resource path, it is used as-is before adding the `sign_in` path/query. If CloudMatch returns an RTSP(S) resource path, OpenNOW derives a `wss://<host>/nvst/` signaling base. The connection also uses the session-bound WebSocket subprotocol `x-nv-sessionid.<session-id>`.

The local `/api/signaling` connection relays browser SDP answers, ICE candidates, and keyframe requests; the server-to-NVIDIA WebSocket carries the GFN peer/signaling protocol. Do not replace the local endpoint with the dynamic NVIDIA host in frontend code.

## 4. Code examples

These examples show the main URLs in context. They mirror the current web client/server behavior; types are illustrative where noted.

### 4.1 Reusable same-origin API helper

```ts
async function api<T>(path: string, init: RequestInit = {}): Promise<T> {
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
    const payload = await response.json().catch(() => null) as { error?: string } | null;
    throw new Error(payload?.error ?? `Request failed (${response.status})`);
  }
  if (response.status === 204) return undefined as T;
  return response.json() as Promise<T>;
}

// Relative path: the current deployment/desktop origin is used automatically.
const { session } = await api<{ session: BrowserSession | null }>("/api/session");
```

In production, never turn that call into `fetch("http://localhost:3000/api/session")`: `localhost` would refer to the visitor's own computer, not the hosted OpenNOW server.

### 4.2 Start and poll NVIDIA device login

```ts
const challenge = await api<DeviceLoginChallengePayload>("/api/auth/device/start", {
  method: "POST",
  body: JSON.stringify({ providerIdpId }), // optional provider selection
});

// Open challenge.verificationUriComplete (or challenge.verificationUri) for the user.
// The verification URL comes from NVIDIA's device-authorize response; it is not fixed.
const result = await api<DeviceLoginPollPayload>("/api/auth/device/poll", {
  method: "POST",
  body: JSON.stringify({ attemptId: challenge.attemptId }),
});
```

The browser receives the user-facing verification link/code, but the `device_code`, OAuth tokens, refresh token, and upstream authorization headers remain server-side.

### 4.3 Read/search catalog and library

```ts
const library = await api<{ games: GameInfo[] }>("/api/library");
const refreshedLibrary = await api<{ games: GameInfo[] }>("/api/library?refresh=1");

const query = encodeURIComponent("racing");
const catalog = await api<CatalogBrowseResult>(`/api/catalog?q=${query}`);
```

### 4.4 Create, poll, and stop a stream

`sessionCreateBody` below is a `SessionCreateRequest`-shaped object **without a token**. The server supplies the token from the encrypted browser session. A complete `settings` object must use the values/types in `src/shared/gfn/session.ts`.

```ts
const sessionCreateBody = {
  appId: "<gfn-app-id>",
  internalTitle: "Example game",
  zone: "np-ams-08",
  streamingBaseUrl: "https://np-ams-08.cloudmatchbeta.nvidiagrid.net/",
  settings, // complete StreamSettings value
};

const session = await api<SessionInfo>("/api/stream/create", {
  method: "POST",
  body: JSON.stringify(sessionCreateBody),
});

const latest = await api<SessionInfo>("/api/stream/poll", {
  method: "POST",
  body: JSON.stringify({
    zone: session.zone,
    sessionId: session.sessionId,
    streamingBaseUrl: session.streamingBaseUrl,
    serverIp: session.serverIp,
  }),
});

await api<void>("/api/stream/stop", {
  method: "POST",
  body: JSON.stringify({
    zone: latest.zone,
    sessionId: latest.sessionId,
    streamingBaseUrl: latest.streamingBaseUrl,
    serverIp: latest.serverIp,
  }),
});
```

### 4.5 Open the same-origin signaling bridge

```ts
const wsScheme = location.protocol === "https:" ? "wss:" : "ws:";
const socket = new WebSocket(`${wsScheme}//${location.host}/api/signaling`);

socket.addEventListener("open", () => {
  socket.send(JSON.stringify({
    type: "connect",
    payload: { sessionId, signalingServer, signalingUrl },
  }));
});

// Other supported client messages include: answer, ice, keyframe, disconnect.
```

`signalingServer` and `signalingUrl` are returned by the session API and are per-session NVIDIA data. `location.host` is what keeps the OpenNOW bridge same-origin in web, preview, and desktop deployments.

### 4.6 Build a region URL and CloudMatch control URL (server-side example)

```ts
import { buildGfnZoneStreamingBaseUrl } from "@shared/gfn/endpoints";

const base = buildGfnZoneStreamingBaseUrl("NP-AMS-08");
// https://np-ams-08.cloudmatchbeta.nvidiagrid.net/

const serverInfoUrl = new URL("v2/serverInfo", base);
// GET serverInfoUrl with server-side GFN headers to discover VPC/region metadata.

const createSessionUrl = new URL("v2/session", base);
createSessionUrl.searchParams.set("keyboardLayout", keyboardLayout);
createSessionUrl.searchParams.set("languageCode", languageCode);
// POST createSessionUrl with the server-built request body and GFN headers.
```

This is **backend-only** code. Do not send a user's NVIDIA token from a browser to a CloudMatch host. OpenNOW also validates client-selected streaming bases: only HTTPS NVIDIA GRID domains are honored; a non-NVIDIA host falls back to the provider-discovered base.

### 4.7 NVIDIA subscription URL construction (server-side example)

```ts
const url = new URL("https://mes.geforcenow.com/v4/subscriptions");
url.searchParams.set("serviceName", "gfn_pc");
url.searchParams.set("languageCode", "en_US");
url.searchParams.set("vpcId", vpcId);
url.searchParams.set("userId", userId);
// The backend sends its GFN client headers and the account token.
```

### 4.8 Steam artwork URL construction

```ts
const steamHeader = `https://cdn.cloudflare.steamstatic.com/steam/apps/${steamAppId}/header.jpg`;
const steamHero = `https://cdn.cloudflare.steamstatic.com/steam/apps/${steamAppId}/library_hero.jpg`;
const steamPortrait = `https://cdn.cloudflare.steamstatic.com/steam/apps/${steamAppId}/library_600x900.jpg`;
```

These image URLs are used for game cards, hero art, and the standalone UI preview. `{steamAppId}` is a numeric Steam app ID from game data or preview seed data.

## 5. NVIDIA / GeForce NOW upstream URLs

The following calls are made by OpenNOW's Node backend, except where explicitly marked as a link, browser asset, or header value. Exact request methods/headers are in `src/server/gfn/`.

### Account, login, and profile

| URL | Use |
|---|---|
| `https://pcs.geforcenow.com/v1/serviceUrls` | Service discovery. `GET` returns GFN identity-provider choices and streaming-service URLs; OpenNOW caches the result and has a default fallback. |
| `https://login.nvidia.com/device/authorize` | `POST` to start link/device authorization. Response includes device/user codes and NVIDIA-generated `verification_uri` / `verification_uri_complete`. |
| `https://login.nvidia.com/token` | `POST` for device-code exchange and OAuth refresh-token grant. This is the endpoint the server polls while a user approves a device login. |
| `https://login.nvidia.com/client_token` | Requests a NVIDIA client token from an access token; used by the server's refresh logic when supported. |
| `https://login.nvidia.com/userinfo` | Profile fallback request when useful profile claims are unavailable in the current ID/access token. |
| `https://login.nvidia.com/authorize` | Declared as an auth constant, but the current web app uses link-based device authorization and does not perform a redirect-based authorize flow. See [inactive/declaration notes](#12-declared-or-documented-urls-that-are-not-live-requests). |
| `https://play.geforcenow.com` and `https://play.geforcenow.com/` | NVIDIA `Origin` and `Referer` header values used to resemble the GFN web client for GraphQL/auth/signaling requests. These are not the upstream API target by themselves. |
| `https://nvfile` and `https://nvfile/` | NVIDIA `Origin` and `Referer` header markers used by the backend's NVIDIA auth-header builder. This is a synthetic/header origin in code, not a standalone API endpoint that OpenNOW navigates to. |

The verification URLs opened by the user are **returned by NVIDIA**, not hardcoded. They can differ by provider, account state, and response. The private device code is stored only in the server-side browser session.

### Catalog, library, game ownership, and service discovery

| URL | Use |
|---|---|
| `https://apps.gxn.nvidia.com/graphql` | LCARS GraphQL endpoint used for POSTed GFN library mutations/operations. |
| `https://games.geforce.com/graphql` | GFN GraphQL endpoint used for catalog, game metadata, app ID/store resolution, and persisted GraphQL query requests. It is the default persisted-query endpoint in the LCARS GraphQL helper. |
| `https://prod.cloudmatchbeta.nvidiagrid.net/` | Default streaming-service base used if provider discovery fails or a provider has no usable base. |
| `https://<streaming-base>/v2/serverInfo` | Dynamic `GET` for VPC ID and region/service metadata. `<streaming-base>` normally comes from NVIDIA service discovery or the default above. |

The two GraphQL URLs are used by server-side code with GFN client headers; they are not direct browser API routes. Some GraphQL responses include dynamic game artwork URLs, store destinations, or additional provider metadata. Those returned URLs are data, not extra fixed OpenNOW constants.

### Subscription, storage, and service status

| URL | Use |
|---|---|
| `https://mes.geforcenow.com/v4/subscriptions` | GFN membership/subscription lookup. Query parameters include `serviceName=gfn_pc`, `languageCode=en_US`, `vpcId=<VPC>`, and `userId=<account-id>`; the helper defaults the VPC to `NP-AMS-08` if one was not discovered. |
| `https://api-prod.nvidia.com/gfn-paywall-api/api/v2/products?locale=<locale>&vpcId=<vpc-id>` | Looks up persistent-storage products/regions. `locale` defaults to `en_US`; `vpcId` is added when known. Request is authenticated server-side using an `idToken` header. |
| `https://api-prod.nvidia.com/gfn-paywall-api/api/v2/reset/storage?storageRegion=<encoded-region>` | `POST` to reset/move the user's GFN persistent storage. The region is URL-encoded; null/empty selection is represented as `storageRegion=null`. |
| `https://status.geforcenow.com/api/v2/components.json` | Reads GFN Statuspage components to construct fallback persistent-storage region availability when paywall region data is unavailable. This request is best-effort and has a 10-second timeout. |
| `https://static.nvidiagrid.net/supported-public-game-list/locales/gfnpc-en-US.json` | Downloads NVIDIA's public supported-game list for `/api/public-games` and to augment catalog/library results. |

### Steam art generated from GFN game metadata

These fixed templates are used by the public-game converter and game UI:

```text
https://cdn.cloudflare.steamstatic.com/steam/apps/{steamAppId}/header.jpg
https://cdn.cloudflare.steamstatic.com/steam/apps/{steamAppId}/library_hero.jpg
https://cdn.cloudflare.steamstatic.com/steam/apps/{steamAppId}/library_600x900.jpg
```

The first two are used by public supported-game data and the home game cards; the portrait and hero variants are also seeded by `src/client/preview.tsx`. The client may additionally render NVIDIA-provided image URLs from catalog metadata. Those are dynamic, so their full hosts/paths depend on the upstream catalog response.

## 6. GeForce NOW region hosts

### URL pattern and discovery

The standard zone helper emits:

```text
https://{zone-id-lowercase}.cloudmatchbeta.nvidiagrid.net/
```

For example:

```ts
buildGfnZoneStreamingBaseUrl("NP-AMS-08");
// "https://np-ams-08.cloudmatchbeta.nvidiagrid.net/"
```

The helper constructs standard `NP-*` hosts using the `cloudmatchbeta.nvidiagrid.net` suffix. Region URLs are normally supplied by NVIDIA service discovery or this helper. The transport validator also recognizes the legacy `cloudmatch.nvidiagrid.net` domain when identifying a CloudMatch zone load balancer; legacy/partner hosts are dynamic and are not enumerated as built-in regions.

OpenNOW gets dynamic region URLs from `GET <streaming-base>/v2/serverInfo`. If that request fails or provides too few regions, `src/server/gfn/subscription.ts` uses the static fallback list below. `src/client/components/settings/sections/SettingsStreamSection.tsx` separately defines the built-in region choices shown in settings; most overlap the fallback list, and some region labels intentionally point to the same host. The exact server/zone availability is controlled by NVIDIA and can change. The browser's latency check then sends an HTTPS `GET` to each candidate region base with `mode: "no-cors"`, no credentials, no referrer, and a six-second timeout; it measures the connection without reading a response body.

### Built-in fallback hosts

| Built-in region label | CloudMatch base URL |
|---|---|
| US Central — Dallas | `https://np-dal-08.cloudmatchbeta.nvidiagrid.net/` |
| US East — Ashburn | `https://np-ash-08.cloudmatchbeta.nvidiagrid.net/` |
| US Midwest — Chicago | `https://np-chi-08.cloudmatchbeta.nvidiagrid.net/` |
| US Northeast — Newark | `https://np-nwk-08.cloudmatchbeta.nvidiagrid.net/` |
| US Northwest — Seattle | `https://np-sea-08.cloudmatchbeta.nvidiagrid.net/` |
| US South — Atlanta | `https://np-atl-08.cloudmatchbeta.nvidiagrid.net/` |
| US Southeast — Miami | `https://np-mia-08.cloudmatchbeta.nvidiagrid.net/` |
| US Southwest — Los Angeles | `https://np-lax-08.cloudmatchbeta.nvidiagrid.net/` |
| US West — San Jose | `https://np-sjc-08.cloudmatchbeta.nvidiagrid.net/` |
| CA East — Montreal | `https://np-yul-08.cloudmatchbeta.nvidiagrid.net/` |
| EU Northeast — Amsterdam / Netherlands North | `https://np-ams-08.cloudmatchbeta.nvidiagrid.net/` |
| EU Central — Frankfurt | `https://np-frk-08.cloudmatchbeta.nvidiagrid.net/` |
| EU Southwest — Paris | `https://np-par-08.cloudmatchbeta.nvidiagrid.net/` |
| EU West — London | `https://np-lhr-08.cloudmatchbeta.nvidiagrid.net/` |
| EU Northwest — Stockholm | `https://np-sto-08.cloudmatchbeta.nvidiagrid.net/` |
| EU Southeast — Sofia | `https://np-sof-02.cloudmatchbeta.nvidiagrid.net/` |
| EU Southeast — Warsaw | `https://np-waw-02.cloudmatchbeta.nvidiagrid.net/` |
| Caucasus South — Yerevan | `https://np-evn-02.cloudmatchbeta.nvidiagrid.net/` |
| Africa South — Johannesburg | `https://np-jnb-02.cloudmatchbeta.nvidiagrid.net/` |
| SA East — Sao Paulo | `https://np-gru-08.cloudmatchbeta.nvidiagrid.net/` |
| LATAM South — Montevideo | `https://np-mvd-02.cloudmatchbeta.nvidiagrid.net/` |
| AU East — Sydney | `https://np-syd-02.cloudmatchbeta.nvidiagrid.net/` |
| AU West — Perth | `https://np-per-02.cloudmatchbeta.nvidiagrid.net/` |
| JP East — Tokyo | `https://np-nrt-08.cloudmatchbeta.nvidiagrid.net/` |
| SG — Singapore | `https://np-sin-02.cloudmatchbeta.nvidiagrid.net/` |
| KR — Seoul | `https://np-icn-02.cloudmatchbeta.nvidiagrid.net/` |
| TR Central — Ankara | `https://np-ank-02.cloudmatchbeta.nvidiagrid.net/` |
| TR West — Istanbul | `https://np-ist-02.cloudmatchbeta.nvidiagrid.net/` |
| TW — Taipei | `https://np-tpe-02.cloudmatchbeta.nvidiagrid.net/` |
| Netherlands South | `https://np-ams-09.cloudmatchbeta.nvidiagrid.net/` |
| Germany North | `https://np-frk-09.cloudmatchbeta.nvidiagrid.net/` |
| UK South | `https://np-lhr-09.cloudmatchbeta.nvidiagrid.net/` |
| France Central | `https://np-par-09.cloudmatchbeta.nvidiagrid.net/` |

The settings UI also labels `np-ams-08` as “Netherlands North”; that is the same URL as “EU Northeast — Amsterdam” and is deduplicated in the server's fallback output. The settings list currently contains a smaller curated subset, while the backend fallback includes Yerevan, Montevideo, Turkey, Taiwan, and Netherlands South.

### CloudMatch paths on a region or server host

The backend appends these paths to the selected/discovered CloudMatch base, or to the game-server IP/hostname returned in the session response:

| Method | Pattern | Purpose |
|---|---|---|
| `GET` | `https://<streaming-base>/v2/serverInfo` | Discover server/VPC metadata and regions. |
| `GET` | `https://<streaming-base>/v2/session` | List sessions; used by active-session discovery. |
| `POST` | `https://<streaming-base>/v2/nettestsession` | Creates/reuses a cached network-test session in WebRTC mode before the game session. Native NVST requests skip this probe. |
| `POST` | `https://<streaming-base>/v2/session?keyboardLayout=<layout>&languageCode=<locale>` | Creates the game session; its request body may include the network-test session ID. |
| `GET` | `https://<streaming-base>/v2/session/<session-id>` | Poll queue/setup/readiness, validate a claim, and retrieve connection/signaling data. |
| `PUT` | `https://<game-server>/v2/session/<session-id>?keyboardLayout=<layout>&languageCode=<locale>` | Claim/resume a session. |
| `PUT` | `https://<game-server>/v2/session/<session-id>` | Report session ad actions/updates. |
| `DELETE` | `https://<game-server>/v2/session/<session-id>` | Stop a session. |

For a ready session, OpenNOW may re-poll directly against the actual server IP returned by CloudMatch rather than the original zone load balancer. `GET /v2/serverInfo` metadata can also reveal additional region base URLs; these are accepted only when they are HTTPS values from the NVIDIA service metadata.

## 7. Streaming, signaling, media, and ICE URLs

### Session-specific NVIDIA signaling and RTSP(S)

CloudMatch returns connection details at runtime. OpenNOW handles the following patterns:

- **NVIDIA signaling:** `wss://<signaling-host>/nvst/` or the session-supplied WSS resource path. The server appends `/sign_in` and session/peer query parameters when connecting.
- **NVIDIA media/control resource:** `rtsps://<media-or-seat-host>:<port>/<resource>` (sometimes an `rtsp://` value is returned). These values are session-specific, may identify a separate host/port, and are never fixed to one server in source.
- **Native RTSP control/ping WebSocket:** the bundled native streamer can derive `wss://<authority-host>:<port>/rtsp` from an RTSP(S) resource for RTSP control/ping. It is also session/server-derived.
- **Direct CloudMatch host:** `https://<game-server-ip-or-host>/v2/session/<session-id>` as described above.
- **Media transport:** CloudMatch also returns IP/port/protocol connection tuples. Actual audio/video can travel over UDP; UDP connection tuples are not HTTP URLs.

If CloudMatch returns a relative signaling resource path, OpenNOW constructs `wss://<server-ip>:443<path>`. If the path cannot be interpreted, the fallback is `wss://<server-ip>:443/nvst/`.

### STUN and TURN/relay services

WebRTC may use ICE servers supplied by the GFN session. These URLs are dynamic and can include `stun:`, `turn:`, or `turns:` URLs with returned usernames/credentials. If the session provides no ICE configuration, the backend has these STUN defaults:

| ICE URL | Purpose |
|---|---|
| `stun:s1.stun.gamestream.nvidia.com:19308` | NVIDIA GameStream STUN fallback. |
| `stun:stun.l.google.com:19302` | Google STUN fallback. |
| `stun:stun1.l.google.com:19302` | Google STUN fallback. |

The browser's WebRTC environment probe and final-resort relay list also reference the public Open Relay service:

| ICE URL | Purpose |
|---|---|
| `turn:openrelay.metered.ca:443?transport=tcp` | TCP TURN relay on port 443. |
| `turn:openrelay.metered.ca:80?transport=tcp` | TCP TURN relay on port 80. |
| `turns:openrelay.metered.ca:443?transport=tcp` | TLS TURN relay on port 443. |

The Open Relay entry currently uses the public `openrelayproject` username/credential from `src/client/platforms/gfn/webrtcClient.ts`. It is intended as a last resort when normal browser ICE gathering yields no usable local candidates (for example, strict firewall/VPN conditions). A relay can increase latency and means relayed WebRTC traffic traverses that third-party relay. GFN-provided ICE configuration takes priority; do not assume that every stream uses these fallback servers.

The CloudMatch helper may resolve STUN hostnames using the OS resolver and fallback DNS resolvers `1.1.1.1` and `8.8.8.8` (Cloudflare and Google IPv4 DNS). These are DNS server IPs, not HTTP API URLs.

## 8. Images, fonts, and external navigation

### Fonts and browser policy

`index.html` and `preview.html` use Google Fonts:

| URL | Purpose |
|---|---|
| `https://fonts.googleapis.com` | Font stylesheet host and `preconnect` target. |
| `https://fonts.gstatic.com` | Font-file host and `preconnect` target. |
| `https://fonts.googleapis.com/css2?family=Montserrat:wght@300..900&family=Roboto:wght@300;400;500;700&family=Source+Sans+3:ital,wght@0,400;0,600;1,400&display=swap` | Stylesheet request for Montserrat, Roboto, and Source Sans 3. In the HTML attribute the ampersands are escaped as `&amp;`. |

The production Content Security Policy in `src/server/index.ts` permits fonts from those Google hosts, HTTPS images, and same-origin/HTTPS/WS/WSS connections. It also permits `data:` images and `blob:` media for local rendering.

### User-facing external links

| URL | How OpenNOW uses it |
|---|---|
| `https://www.nvidia.com/en-us/account/gfn/manage-storage/` | “Manage storage” link in account settings; opened externally. The upstream paywall API handles the in-app storage location/reset operations. |
| `https://github.com/OpenCloudGaming/OpenNOW/releases/tag/v<version>` | “View on GitHub” link in the release-highlights dialog. The version is inserted at runtime. |
| `https://github.com/sponsors/zortos293` | Sponsor link shown alongside the optional community proxy setting. |
| `https://printedwaste.com/gfn` | PrintedWaste attribution / queue-information link in the queue server selector. |
| NVIDIA device verification URL | Returned by NVIDIA's `/device/authorize` response and shown to the user; dynamic, not a fixed URL in the UI. |
| Game store link | Returned dynamically from GeForce NOW game-variant metadata through `/api/resolve-store-url`; can point at the store relevant to that title/variant. |
| Release-note Markdown link | Links in a `ReleaseHighlightsPayload.bodyMarkdown` value are opened externally by the release dialog. The hosted web shim currently uses a short fallback body with no remote notes; any supplied link is dynamic. |
| Thank-you profile URL | `SettingsThanksSection` can render a `profileUrl` supplied by the community-data bridge. The hosted web bridge currently returns empty contributor/supporter lists, so those profile links are not populated in the web build. |

Open external links in the web shim are limited to `http:` and `https:` and use a new tab with `noopener,noreferrer`. The desktop shell delegates trusted external links to the OS browser.

### Dynamic image and profile URLs

- **NVIDIA catalog images:** image fields returned from GFN GraphQL are rendered as remote image URLs; exact hosts and paths vary with upstream game data.
- **Steam artwork:** templates are in [section 4.8](#48-steam-artwork-url-construction) and are expanded with the numeric Steam app ID.
- **Gravatar:** `https://www.gravatar.com/avatar/<md5-of-normalized-email>?s=<size>&d=identicon` is used as an avatar fallback when NVIDIA does not supply a picture. The normal size is 80 pixels and the email is lower-cased/trimmed before hashing.
- **Community thanks data:** `avatarUrl` image and `profileUrl` link fields can be supplied by the bridge, but the hosted web shim currently returns empty contributor/supporter lists; those remote URLs are not populated in the web build.
- **Preview UI artwork:** `src/client/preview.tsx` uses the same Steam CDN host and templates with a local seed list of app IDs. The preview page is a development-only standalone launcher preview.
- **Inline generated URLs:** a small `data:image/svg+xml,...` noise texture is embedded by `src/client/styles.css`; screenshot thumbnails can also be data URLs. Browser-created media/recording object URLs use `blob:` and are local to that browser session.

## 9. Proxy URLs and user-supplied URLs

### Session proxy formats

The session-proxy setting accepts a host/port with no scheme (treated as `http://`) or an explicit URL with one of these schemes. The settings input uses `http://127.0.0.1:8080` as an example placeholder; it is not a required proxy address:

```text
http://host:port
https://host:port
socks4://host:port
socks5://host:port
```

The port and host are required. Usernames/passwords, if present, are encoded in the URL user-info section. The proxy is used server-side for eligible GFN/CloudMatch fetches; it is not an OpenNOW API route. Never paste a real credential-bearing proxy URL into source control or a bug report.

### Zortos community proxy constants

The source declares these values in `src/shared/communityProxy.ts`:

| URL / host | Purpose / status |
|---|---|
| `https://opennow-proxy.zortos.me/api/public/proxy` | Community proxy provisioning endpoint constant. The hosted web shim currently rejects provisioning as unavailable; this is not an active fetch in the web build. |
| `http://<encoded-user>:<encoded-password>@opennow-proxy-tcp.zortos.me:3128` | Proxy URL shape built from provisioning credentials for the TCP proxy. The password must remain secret. |
| `opennow-proxy-tcp.zortos.me:3128` | Primary community proxy host/port. |
| `217.76.50.166:3128` | Fallback address recognized by the community-proxy URL detector; the builder uses the hostname by default. |
| `https://github.com/sponsors/zortos293` | Sponsor link shown by the settings UI. |

A web user can still configure a normal session proxy URL, but the separate community-provisioning bridge is unavailable in this hosted web implementation. The provisioning URL is therefore documented as a declaration, not a current request.

## 10. Development, preview, desktop, and deployment origins

| Origin / pattern | Where it applies |
|---|---|
| `http://localhost:3000` | Default local Node/Vite development server (`PORT` defaults to 3000), `npm run dev`, the Tauri dev URL, and README instructions. |
| `http://127.0.0.1:<port>` | Installed desktop WebView origin. Tauri starts the bundled backend on its selected port and loads this loopback origin so cookies and signaling stay same-origin. The launcher prefers port `48127` when available (or `OPENNOW_DESKTOP_PORT`), then falls back to a free loopback port. |
| `http://localhost:*/*` and `http://127.0.0.1:*/*` | Tauri remote-backend capability patterns, allowing the desktop shell bridge on loopback origins. |
| `http://localhost:5173` | Default local address for `npm run preview:ui` (Vite preview config; serves `preview.html` with mock data and no backend/auth). |
| `http://localhost:3000/api/session` | Default target for `npm run test:load`; `LOAD_TEST_URL` can override it. This is a developer load-test target, not a public service. |
| `https://<your-public-domain>` | Example production HTTPS origin. `OPENNOW_ORIGIN` should be set to the exact public scheme/host/port so the `/api/signaling` Origin check is strict. The example `your-domain.example` in README is a placeholder, not an actual OpenNOW domain. |
| `https://<tunnel-host>` | A tunnel/preview origin. Vite development host allowlists can be extended with `OPENNOW_DEV_ALLOWED_HOSTS`; the server includes the `.e2b.app` host suffix for hosted previews. These are incoming Host/origin allowances, not outbound service URLs; the browser still calls relative `/api/...` paths in normal app mode. |

The production deployment should be served over HTTPS behind a reverse proxy that forwards WebSocket upgrades for `/api/signaling`. Desktop local URLs intentionally use HTTP on loopback; do not treat them as public deployment URLs. The URL in `src-tauri/src/main.rs` is an origin, not a separate API host—the same `/api/...` paths are used there.

## 11. Project and documentation references

These are repository metadata or developer-facing documentation links, not OpenNOW runtime APIs:

| URL | Location / purpose |
|---|---|
| `https://github.com/OpenCloudGaming/Opennow-web#readme` | `package.json` homepage metadata. |
| `https://github.com/OpenCloudGaming/Opennow-web.git` | `package.json` repository metadata. |
| `https://github.com/OpenCloudGaming/Opennow-web/issues` | `package.json` issue tracker metadata. |
| `https://github.com/Gmaafdh/Opennow-web` | Repository metadata in `src-tauri/Cargo.toml`; differs from the package metadata and is not contacted at runtime. |
| `https://schema.tauri.app/config/2` | Tauri configuration JSON schema reference in `src-tauri/tauri.conf.json` and `src-tauri/tauri.ci.conf.json`. |
| `https://v2.tauri.app` | Tauri documentation link in `docs/DESKTOP.md`. |
| `https://rustup.rs` | Rust toolchain installation link in `docs/DESKTOP.md`. |
| `https://doc.rust-lang.org/cargo/reference/manifest.html` | Cargo manifest reference in `src-tauri/Cargo.toml`. |
| `https://w3c.github.io/gamepad/#remapping` | Gamepad remapping specification comment in `src/client/platforms/gfn/inputProtocol.ts`; code reference only. |

`https://your-domain.example` and `https://your-tunnel.example` are illustrative placeholders in README instructions. They should be replaced with the deployer's actual origin/domain and are not application endpoints.

## 12. Declared or documented URLs that are not live requests

This section prevents similarly named constants/comments from being mistaken for active network calls:

- `https://login.nvidia.com/authorize` is declared in the NVIDIA auth constants, but the current product flow is device-link login; no redirect-based authorization request is issued.
- `https://opennow-proxy.zortos.me/api/public/proxy` is declared as the community-proxy provisioning URL, but `src/client/api.ts` reports community-proxy provisioning as unavailable in the hosted web build.
- `https://api.printedwaste.com/gfn/queue/` appears in a type comment in `src/shared/gfn/printedWaste.ts`. The web bridge currently returns empty queue/mapping objects rather than calling that API. The active user-facing PrintedWaste link is `https://printedwaste.com/gfn`.
- `https://api.printedwaste.com/...` server-mapping/queue endpoints should not be described as an active web server request without adding an implementation; their current shared types do not fetch them.
- NVIDIA-provided `verification_uri`, catalog images/store links, streaming service URLs, CloudMatch game-server addresses, signaling paths, and per-session ICE addresses are runtime data. Their shape is documented above, but no finite static URL list can cover their changing hosts/values.
- Test fixtures, including `example.invalid` art/profile URLs and fake session values, are intentionally excluded because they are not production requests.

## 13. Source files to consult

| Source | Relevance |
|---|---|
| `src/server/api.ts` | All first-party HTTP API route definitions, methods, auth behavior, and payload handling. |
| `src/client/api.ts` | Browser helper headers/cookies, route calls, same-origin WebSocket URL, and region HTTPS probes. |
| `src/server/signalingBridge.ts` | `/api/signaling` upgrade, origin checks, and relay message protocol. |
| `src/server/webAuth.ts` | Provider discovery, default CloudMatch base, browser login/session flow. |
| `src/server/gfn/auth/constants.ts` | NVIDIA identity endpoints and login constants. |
| `src/server/gfn/auth/deviceLogin.ts`, `tokenRefresh.ts`, `userInfo.ts` | Login/token/profile requests and Gravatar URL generation. |
| `src/shared/gfn/endpoints.ts` | Shared GFN web origins, storage-manager link, and zone URL builder. |
| `src/server/gfn/subscription.ts` | MES subscription endpoint, dynamic regions, and all region fallback entries. |
| `src/server/gfn/cloudmatch.ts`, `cloudmatchTransport.ts`, `cloudmatchSessionRequest.ts`, `cloudmatchSignaling.ts` | CloudMatch session/net-test routes, selected base validation, game-server resolution, signaling and ICE URLs. |
| `src/server/gfn/lcarsGraphql.ts`, `gameAppMapper.ts`, `catalogBrowse.ts`, `libraryGames.ts` | GFN GraphQL endpoints and catalog/library/game-store integrations. |
| `src/server/gfn/paywall.ts`, `persistentStorage.ts` | Paywall products/reset and GFN Statuspage URL. |
| `src/server/gfn/publicGames.ts` | NVIDIA supported-game JSON and Steam artwork templates. |
| `src/server/gfn/clientHeaders.ts` | GFN Origin/Referer values used for upstream requests. |
| `src/shared/communityProxy.ts` | Zortos community proxy URL/host constants and builder. |
| `src/shared/gfn/printedWaste.ts` and the web stubs in `src/client/api.ts` | PrintedWaste API type-comment URL and current empty/unavailable hosted-web bridge behavior. |
| `src/client/platforms/gfn/webrtcClient.ts` | Browser STUN/TURN fallbacks and ICE behavior. |
| `third_party/opennow-streamer/crates/opennow-streamer-core/src/nvst_rtsp.rs` | Native sidecar's RTSP(S)-derived control-WebSocket URL pattern. |
| `src/client/components/settings/sections/SettingsStreamSection.tsx` | Curated region settings URLs and proxy controls. |
| `src/server/index.ts` | Production CSP, port default, API no-store behavior, and web server origin logging. |
| `src-tauri/tauri.conf.json`, `src-tauri/src/main.rs`, `src-tauri/capabilities/remote-backend.json` | Desktop/dev WebView origins and loopback capability patterns. |
| `index.html`, `preview.html` | Google Fonts stylesheet/preconnect URLs. |
| `src/client/components/HomePage.tsx`, `src/client/preview.tsx` | Steam CDN artwork and preview seed URLs. |
