# GeForce NOW / NVIDIA URL Inventory Used by OpenNOW

This document is a focused inventory of the **upstream GeForce NOW (GFN) and NVIDIA URLs and URL patterns used by OpenNOW**. It is intended as input for reimplementing the GFN integration in a separate client.

It deliberately does **not** catalogue OpenNOW's application-internal HTTP/WebSocket paths, WebView/development origins, GitHub/documentation links, fonts, Steam artwork, Gravatar, PrintedWaste, or community-proxy services. Those are not GFN endpoints. URLs that NVIDIA returns dynamically (provider bases, game servers, signaling hosts, verification links, ICE servers) are described by pattern; NVIDIA can change those values and region availability.

> **Credential and protocol warning:** OAuth access/ID/refresh/client tokens and GFN session tokens are sensitive. Keep them server-side or in an appropriately protected native client; never put them in a URL or commit real tokens. NVIDIA endpoints use client-specific headers and request shapes. This inventory records what the current OpenNOW source calls, not a guarantee of public API stability or authorization to use an endpoint.

## 1. GFN URL flow at a glance

Typical account-to-stream flow:

1. Discover identity providers and a provider-specific `streamingServiceUrl` from `https://pcs.geforcenow.com/v1/serviceUrls`.
2. Start link/device authorization at `https://login.nvidia.com/device/authorize`; show the returned verification URL and code to the user.
3. Poll/exchange the device code at `https://login.nvidia.com/token`; optionally request `https://login.nvidia.com/client_token` and profile data from `https://login.nvidia.com/userinfo`.
4. Use the discovered streaming base (or the fallback `https://prod.cloudmatchbeta.nvidiagrid.net/`) for `/v2/serverInfo` and to discover regions/VPC metadata.
5. Use GFN GraphQL, MES, public-game-list, and paywall/status endpoints for catalog, membership, and storage information.
6. Create a WebRTC network-test session and game session through the chosen CloudMatch region; poll/claim/stop them through CloudMatch session endpoints.
7. Read the resulting game-server, signaling, media and ICE endpoints from the session response. They are session-specific; do not hardcode a single game-server IP.

All fixed GFN URL literals currently present in relevant OpenNOW source are grouped below. The static region inventory is in [section 4](#4-built-in-cloudmatch-region-urls).

---

## 2. Account discovery and NVIDIA authentication

| URL | Method | Use |
|---|---:|---|
| `https://pcs.geforcenow.com/v1/serviceUrls` | `GET` | GFN service discovery. The response contains `gfnServiceInfo.gfnServiceEndpoints`; each entry can supply an IdP ID, provider code/name, priority and `streamingServiceUrl`. OpenNOW sorts/caches providers and normalizes the streaming base to end in `/`. |
| `https://login.nvidia.com/device/authorize` | `POST` | Starts NVIDIA device authorization. OpenNOW sends URL-encoded form data including `client_id`, `scope`, `device_id`, `display_name`, and `idp_id`. NVIDIA returns `device_code`, `user_code`, expiry/poll interval, and user-facing `verification_uri` / `verification_uri_complete`. The verification URLs are dynamic response values, not fixed OpenNOW URLs. |
| `https://login.nvidia.com/token` | `POST` | Actively used for the OAuth device-code exchange and refresh-token grant. A client-token grant helper is also defined in source but is not called by the current web auth flow. The exact grant is selected through form fields (`grant_type`, `device_code` or `refresh_token`/`client_token`, `client_id`, and related values). |
| `https://login.nvidia.com/client_token` | `GET` | Requests a NVIDIA client token using the account access token. The response contains `client_token` and lifetime information. |
| `https://login.nvidia.com/userinfo` | `GET` | Profile fallback if the ID/access-token claims do not contain the profile fields needed by OpenNOW. Uses bearer authorization and GFN client headers. |
| `https://login.nvidia.com/authorize` | — | Defined as an auth constant, but the current OpenNOW web flow is link-based device authorization; it does **not** perform a redirect-based authorize request. |

### Device authorization request shape

The device authorization request is form-urlencoded; the verification URL is read from the response:

```ts
const response = await fetch("https://login.nvidia.com/device/authorize", {
  method: "POST",
  headers: {
    ...authHeaders,
    "Content-Type": "application/x-www-form-urlencoded; charset=UTF-8",
    "x-device-id": deviceId,
    "nv-client-id": clientId,
    "nv-client-streamer": "WEBRTC",
    "nv-client-type": "BROWSER",
    "nv-client-platform-name": "browser",
    "nv-browser-type": "CHROME",
    "nv-device-os": "STEAMOS",
    "nv-device-type": "CONSOLE",
    "nv-device-model": "STEAMDECK",
    "nv-device-make": "VALVE",
  },
  body: new URLSearchParams({
    client_id: clientId,
    scope: "openid consent email tk_client age",
    device_id: deviceId,
    display_name: "OpenNOW",
    idp_id: providerIdpId,
  }),
});

const { device_code, user_code, verification_uri, verification_uri_complete } =
  await response.json();
```

### Token grants

```ts
// Device-code exchange (POST https://login.nvidia.com/token)
const deviceGrant = new URLSearchParams({
  grant_type: "urn:ietf:params:oauth:grant-type:device_code",
  device_code: deviceCode,
  client_id: clientId,
});

// Refresh grant (same token URL)
const refreshGrant = new URLSearchParams({
  grant_type: "refresh_token",
  refresh_token: refreshToken,
  client_id: clientId,
});

// Client-token grant helper is defined in source but is not used by the current web auth flow.
const clientGrant = new URLSearchParams({
  grant_type: "urn:ietf:params:oauth:grant-type:client_token",
  client_token: clientToken,
  client_id: clientId,
  sub: userId,
});
```

OpenNOW has a standard `CLIENT_ID` and a Steam Deck-style `STEAM_DECK_CLIENT_ID` in `src/server/gfn/auth/constants.ts`. Device authorization uses the Steam Deck client ID; the normal auth helper defaults to the standard client ID unless a token snapshot specifies otherwise. These are client identifiers, not user credentials.

---

## 3. GFN request origins and common header values

These values appear in NVIDIA `Origin`/`Referer` headers; they are **not necessarily the destination URL** for the request:

| Header value | Usage |
|---|---|
| `https://play.geforcenow.com` | GFN `Origin` for GraphQL and signaling requests and some upstream auth/client requests. |
| `https://play.geforcenow.com/` | GFN `Referer` value. |
| `https://nvfile` | NVIDIA file/auth helper `Origin` marker. |
| `https://nvfile/` | Corresponding `Referer` marker. |

GFN GraphQL/CloudMatch helpers use client headers such as `nv-client-id`, `nv-client-type`, `nv-client-version`, `nv-client-streamer`, `nv-device-os`, and `nv-device-type`. CloudMatch bearer-like session authorization is formatted as `Authorization: GFNJWT <token>`; NVIDIA account endpoints may instead use `Bearer <token>`. The paywall API uses an `idToken` header. The correct helper/header family varies by endpoint; do not blindly use one authorization scheme for all hosts.

---

## 4. Built-in CloudMatch region URLs

### Base URL pattern

The standard NVIDIA zone helper constructs:

```text
https://{zone-id-lowercase}.cloudmatchbeta.nvidiagrid.net/
```

Example:

```ts
const streamingBase = "https://" + zoneId.toLowerCase() + ".cloudmatchbeta.nvidiagrid.net/";
// NP-AMS-08 -> https://np-ams-08.cloudmatchbeta.nvidiagrid.net/
```

The provider-discovered `streamingServiceUrl` is preferred. If discovery fails, OpenNOW's fallback is:

```text
https://prod.cloudmatchbeta.nvidiagrid.net/
```

The backend also recognizes the legacy `cloudmatch.nvidiagrid.net` zone suffix. Provider/alliance endpoints may be returned dynamically and need not match the static `cloudmatchbeta` region table. NVIDIA service discovery and CloudMatch `serverInfo` responses are the source of truth for a live account/region.

### Static fallback inventory

The server's fallback code contains 34 named entries / 33 unique host URLs. The settings UI exposes a curated subset (27 unique hosts, with Amsterdam listed under two labels). The six rows marked **fallback-only** are in the server fallback list but not in the current settings region list.

| Region / label | Zone host URL | Source |
|---|---|---|
| US Central — Dallas | `https://np-dal-08.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| US East — Ashburn | `https://np-ash-08.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| US Midwest — Chicago | `https://np-chi-08.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| US Northeast — Newark | `https://np-nwk-08.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| US Northwest — Seattle | `https://np-sea-08.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| US South — Atlanta | `https://np-atl-08.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| US Southeast — Miami | `https://np-mia-08.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| US Southwest — Los Angeles | `https://np-lax-08.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| US West — San Jose | `https://np-sjc-08.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| CA East — Montreal | `https://np-yul-08.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| EU Northeast — Amsterdam / Netherlands North | `https://np-ams-08.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback; same URL under two labels |
| EU Central — Frankfurt | `https://np-frk-08.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| EU Southwest — Paris | `https://np-par-08.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| EU West — London | `https://np-lhr-08.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| EU Northwest — Stockholm | `https://np-sto-08.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| EU Southeast — Sofia | `https://np-sof-02.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| EU Southeast — Warsaw | `https://np-waw-02.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| Caucasus South — Yerevan | `https://np-evn-02.cloudmatchbeta.nvidiagrid.net/` | Fallback-only |
| Africa South — Johannesburg | `https://np-jnb-02.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| SA East — Sao Paulo | `https://np-gru-08.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| LATAM South — Montevideo | `https://np-mvd-02.cloudmatchbeta.nvidiagrid.net/` | Fallback-only |
| AU East — Sydney | `https://np-syd-02.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| AU West — Perth | `https://np-per-02.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| JP East — Tokyo | `https://np-nrt-08.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| SG — Singapore | `https://np-sin-02.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| KR — Seoul | `https://np-icn-02.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| TR Central — Ankara | `https://np-ank-02.cloudmatchbeta.nvidiagrid.net/` | Fallback-only |
| TR West — Istanbul | `https://np-ist-02.cloudmatchbeta.nvidiagrid.net/` | Fallback-only |
| TW — Taipei | `https://np-tpe-02.cloudmatchbeta.nvidiagrid.net/` | Fallback-only |
| Netherlands South | `https://np-ams-09.cloudmatchbeta.nvidiagrid.net/` | Fallback-only |
| Germany North | `https://np-frk-09.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| UK South | `https://np-lhr-09.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |
| France Central | `https://np-par-09.cloudmatchbeta.nvidiagrid.net/` | Settings + fallback |

### Region discovery and server metadata

The base `/v2/serverInfo` endpoint is used to get VPC/server metadata and dynamic regions. The GFN response may contain a `gfn-regions` list and per-region HTTPS URLs; when discovery fails or returns too few regions, OpenNOW merges/falls back to the static list above.

```ts
const infoUrl = new URL("v2/serverInfo", streamingBase);
const response = await fetch(infoUrl, { headers: gfnHeaders });
```

OpenNOW validates a client-selected region base as HTTPS on an NVIDIA GRID hostname before using it; untrusted arbitrary hostnames are rejected in favor of the provider base. The browser's region latency check performs an HTTPS `GET` against each chosen region base with `mode: "no-cors"`, credentials omitted, no referrer, and a six-second timeout. This is a latency probe, not an API data read.

---

## 5. CloudMatch session API URL patterns

All the following URLs are rooted at the selected provider/region `streamingBaseUrl`, unless the row says `<game-server>`. That host is normally an NVIDIA GRID zone load balancer. Most URLs require GFN session authorization and client/device headers.

| Method | URL pattern | Purpose |
|---|---|---|
| `GET` | `https://<streaming-base>/v2/serverInfo` | Region/VPC/server discovery; also used to discover fallback regions when looking up active sessions. |
| `POST` | `https://<streaming-base>/v2/nettestsession` | Creates the network-test session used before a WebRTC game session. OpenNOW caches the returned network-test session ID. Native NVST requests skip this probe. |
| `GET` | `https://<streaming-base>/v2/session` | Lists sessions for the current GFN account; OpenNOW filters active/ready/streaming sessions. |
| `POST` | `https://<streaming-base>/v2/session?keyboardLayout=<layout>&languageCode=<locale>` | Creates the game session. The JSON body includes game/app and streaming settings and may include the network-test session ID. |
| `GET` | `https://<streaming-base-or-game-server>/v2/session/<session-id>` | Polls queue/setup/ready status and returns server, signaling, media and ICE connection information. Also used to resolve the actual game-server IP before claim. |
| `PUT` | `https://<game-server>/v2/session/<session-id>?keyboardLayout=<layout>&languageCode=<locale>` | Claims/resumes a paused session. `keyboardLayout` and `languageCode` are query parameters. |
| `PUT` | `https://<game-server>/v2/session/<session-id>` | Reports GFN session ad events/actions (start, pause, resume, finish, cancel) and timing data. |
| `DELETE` | `https://<game-server>/v2/session/<session-id>` | Stops the session. |

`<game-server>` is taken from CloudMatch `connectionInfo` / `sessionControlInfo`. It may initially be the zone load balancer and later become a real IP or host. OpenNOW may re-poll the direct server once a session is ready. Do not assume the zone URL and game-server URL are interchangeable for mutations: the claim `PUT` is sent to the resolved game server.

### Example: construct session URLs safely

```ts
const base = streamingBaseUrl.replace(/\/$/, "");

const netTestUrl = `${base}/v2/nettestsession`;
const createUrl = new URL(`${base}/v2/session`);
createUrl.searchParams.set("keyboardLayout", keyboardLayout);
createUrl.searchParams.set("languageCode", languageCode);

const pollUrl = `${base}/v2/session/${encodeURIComponent(sessionId)}`;
const claimUrl = new URL(`https://${gameServer}/v2/session/${encodeURIComponent(sessionId)}`);
claimUrl.searchParams.set("keyboardLayout", keyboardLayout);
claimUrl.searchParams.set("languageCode", languageCode);
const stopUrl = `https://${gameServer}/v2/session/${encodeURIComponent(sessionId)}`;
```

---

## 6. GFN GraphQL, catalog, and game metadata

| URL | Method / request style | Use |
|---|---|---|
| `https://apps.gxn.nvidia.com/graphql` | `POST` JSON GraphQL | LCARS endpoint used by OpenNOW's GFN library mutation/operation helper. |
| `https://games.geforce.com/graphql` | Persisted-query `GET` with query parameters; also `POST` JSON for app metadata/ID and store resolution | GFN catalog panels/search, library/catalog game metadata, app/store mapping, and persisted-query operations. It is the default persisted-query endpoint in the LCARS helper. |

OpenNOW uses persisted-query request definitions and hashes for some catalog operations. The request may retry a failed persisted query with the full GraphQL query where supported. GraphQL responses also supply dynamic GFN image paths, variant IDs, store identifiers, and store URLs; these response values are not a fixed URL list.

### Example: app metadata GraphQL request

```ts
const response = await fetch("https://games.geforce.com/graphql", {
  method: "POST",
  headers: {
    ...gfnGraphqlHeaders,
    "Content-Type": "application/json",
  },
  body: JSON.stringify({ query, variables }),
});
```

`gfnGraphqlHeaders` must be built using the expected GFN client metadata and token format; do not copy a browser token into a public client.

---

## 7. Membership, storage, public catalog, and GFN status

| URL | Method | Use and parameters |
|---|---:|---|
| `https://mes.geforcenow.com/v4/subscriptions` | `GET` | GFN membership and subscription lookup. Query: `serviceName=gfn_pc`, `languageCode=en_US`, `vpcId=<VPC>`, `userId=<account-id>`. OpenNOW defaults `vpcId` to `NP-AMS-08` if none was discovered. Uses GFN LCARS headers and an account token. |
| `https://api-prod.nvidia.com/gfn-paywall-api/api/v2/products` | `GET` | Finds persistent-storage products/regions. Query includes `locale=en_US`; `vpcId=<VPC>` is appended when available. Authenticated using an `idToken` header. |
| `https://api-prod.nvidia.com/gfn-paywall-api/api/v2/reset/storage?storageRegion=<encoded-region>` | `POST` | Requests a persistent-storage reset/move. The selected region is URL-encoded; when absent, OpenNOW sends `storageRegion=null`. Uses `idToken` auth headers. |
| `https://status.geforcenow.com/api/v2/components.json` | `GET` | GFN Statuspage component list, used as fallback data for persistent-storage region availability if paywall product data is unavailable. Best-effort, 10-second timeout. |
| `https://static.nvidiagrid.net/supported-public-game-list/locales/gfnpc-en-US.json` | `GET` | NVIDIA's public supported-game JSON used to populate/augment public catalog results. |

### Example: MES query URL

```ts
const mesUrl = new URL("https://mes.geforcenow.com/v4/subscriptions");
mesUrl.searchParams.set("serviceName", "gfn_pc");
mesUrl.searchParams.set("languageCode", "en_US");
mesUrl.searchParams.set("vpcId", vpcId ?? "NP-AMS-08");
mesUrl.searchParams.set("userId", userId);
// GET mesUrl with GFN LCARS headers; keep the account token private.
```

---

## 8. Per-session signaling, native RTSP(S), and ICE URLs

These URLs are mostly returned by the CloudMatch session response and are therefore dynamic. They are critical for streaming, but they should be read from the session response rather than guessed from a static list.

### NVIDIA signaling WebSocket

The server-side GFN signaling client uses the session's `signalingUrl` when available; otherwise it builds a base using `signalingServer`. Common derived sign-in URL:

```text
wss://<signaling-host>/nvst/sign_in?peer_id=<generated-peer>&version=2&peer_role=1&pairing_id=<session-id>
```

Construction rules in OpenNOW:

- A CloudMatch `rtsps://<host>:<port>` or `rtsp://<host>:<port>` resource is converted to `wss://<host>/nvst/` for signaling.
- A full `wss://...` resource is used as the signaling base.
- A relative path such as `/nvst/` becomes `wss://<server-ip>:443/nvst/`.
- If no valid resource path is returned, fallback is `wss://<server-ip>:443/nvst/`.
- OpenNOW appends `/sign_in` and query keys `peer_id`, `version=2`, `peer_role=1`, and `pairing_id=<sessionId>`.
- WebSocket subprotocol: `x-nv-sessionid.<session-id>`.

This is the GFN/NVIDIA-facing socket. Any application-internal signaling relay is outside this GFN-only URL inventory.

### RTSP(S) media/control URLs

CloudMatch may return classic/NVST connection resources such as:

```text
rtsps://<seat-or-media-host>:<port>/<resource>
rtsp://<seat-or-media-host>:<port>/<resource>
```

OpenNOW uses `usage=14` session connection entries to derive RTSP(S) endpoints and signaling hosts. The bundled native streamer may also derive a control/ping WebSocket:

```text
wss://<authority-host>:<port>/rtsp
```

These are per-session hosts and ports. CloudMatch may separately return media IP/port/protocol tuples for UDP streaming; those tuples are not HTTP URLs.

### ICE/STUN/TURN endpoints

CloudMatch can return `iceServerConfiguration.iceServers` with dynamic `stun:`, `turn:`, and `turns:` URLs and associated credentials. OpenNOW uses that response when present. If no ICE configuration is returned, the backend's STUN defaults are:

| ICE URL | Type / use |
|---|---|
| `stun:s1.stun.gamestream.nvidia.com:19308` | NVIDIA GameStream STUN fallback. |
| `stun:stun.l.google.com:19302` | Google STUN fallback. |
| `stun:stun1.l.google.com:19302` | Google STUN fallback. |

The browser also has a last-resort public TURN relay set for environments where local ICE gathering is blocked:

| ICE URL | Type / use |
|---|---|
| `turn:openrelay.metered.ca:443?transport=tcp` | TCP TURN relay, port 443. |
| `turn:openrelay.metered.ca:80?transport=tcp` | TCP TURN relay, port 80. |
| `turns:openrelay.metered.ca:443?transport=tcp` | TLS TURN relay, port 443. |

These Metered Open Relay and Google URLs are **not NVIDIA/GFN endpoints**; they are third-party connectivity fallbacks referenced by OpenNOW's GFN WebRTC client. The relay currently uses the public `openrelayproject` credential in source. Relay mode can add latency and routes relayed traffic through that third party. The CloudMatch helper may also use DNS resolvers `1.1.1.1` and `8.8.8.8` as fallback hostname resolvers; these are DNS server IPs, not HTTP APIs.

---

## 9. NVIDIA account page link

The account settings UI also opens this NVIDIA/GFN page in a browser:

```text
https://www.nvidia.com/en-us/account/gfn/manage-storage/
```

This is a user-facing account page, not the paywall API. In-app storage locations/reset are handled by the `/products` and `/reset/storage` paywall endpoints in [section 7](#7-membership-storage-public-catalog-and-gfn-status).

---

## 10. Compact endpoint checklist

### Fixed GFN/NVIDIA network endpoints

```text
https://pcs.geforcenow.com/v1/serviceUrls
https://login.nvidia.com/device/authorize
https://login.nvidia.com/token
https://login.nvidia.com/client_token
https://login.nvidia.com/userinfo
https://login.nvidia.com/authorize              # declared; not used by current device-link flow
https://play.geforcenow.com                    # Origin header
https://play.geforcenow.com/                   # Referer header
https://nvfile                                 # Origin/header marker
https://nvfile/                                # Referer/header marker
https://apps.gxn.nvidia.com/graphql
https://games.geforce.com/graphql
https://prod.cloudmatchbeta.nvidiagrid.net/
https://mes.geforcenow.com/v4/subscriptions
https://api-prod.nvidia.com/gfn-paywall-api/api/v2/products
https://api-prod.nvidia.com/gfn-paywall-api/api/v2/reset/storage
https://status.geforcenow.com/api/v2/components.json
https://static.nvidiagrid.net/supported-public-game-list/locales/gfnpc-en-US.json
https://www.nvidia.com/en-us/account/gfn/manage-storage/  # user-facing account page
```

### Dynamic GFN/NVIDIA URL patterns

```text
https://{zone}.cloudmatchbeta.nvidiagrid.net/
https://<provider-streaming-base>/v2/serverInfo
https://<streaming-base>/v2/nettestsession
https://<streaming-base>/v2/session
https://<streaming-base>/v2/session?keyboardLayout=<layout>&languageCode=<locale>
https://<streaming-base-or-game-server>/v2/session/<session-id>
https://<game-server>/v2/session/<session-id>?keyboardLayout=<layout>&languageCode=<locale>
wss://<signaling-host>/nvst/sign_in?peer_id=<peer>&version=2&peer_role=1&pairing_id=<session-id>
rtsps://<seat-or-media-host>:<port>/<resource>
```

---

## 11. Source map for the GFN URL inventory

| Source file | URL-related implementation |
|---|---|
| `src/server/gfn/auth/constants.ts` | NVIDIA identity, service discovery, and token endpoint constants. |
| `src/server/gfn/auth/deviceLogin.ts` | Device authorization and device-code exchange. |
| `src/server/gfn/auth/tokenRefresh.ts` | Refresh/client token requests. |
| `src/server/gfn/auth/userInfo.ts` | NVIDIA userinfo fallback. |
| `src/server/webAuth.ts` | Provider discovery and default CloudMatch streaming base. |
| `src/shared/gfn/endpoints.ts` | GFN play origin/referer, storage manager link, and zone URL builder. |
| `src/server/gfn/clientHeaders.ts`, `src/server/gfn/auth/helpers.ts` | GFN/NVIDIA request header conventions. |
| `src/server/gfn/subscription.ts` | MES endpoint, `serverInfo`-based region discovery, and static fallback regions. |
| `src/server/gfn/cloudmatchSessionRequest.ts` | `/v2/nettestsession` request. |
| `src/server/gfn/cloudmatch.ts` | Create/poll/claim/report/stop/active-session CloudMatch URL patterns. |
| `src/server/gfn/cloudmatchTransport.ts` | Region base validation, `serverInfo`, URL normalization, zone/server host selection. |
| `src/server/gfn/cloudmatchSignaling.ts`, `src/server/gfn/signaling.ts` | Dynamic WSS URL construction and NVIDIA signaling connection. |
| `src/server/gfn/lcarsGraphql.ts`, `src/server/gfn/gameAppMapper.ts` | GraphQL endpoints and catalog/app/store mapping. |
| `src/server/gfn/paywall.ts`, `src/server/gfn/persistentStorage.ts` | NVIDIA paywall/storage endpoints and GFN status fallback. |
| `src/server/gfn/publicGames.ts` | NVIDIA supported-public-game-list endpoint. |
| `src/client/platforms/gfn/webrtcClient.ts` | GFN WebRTC ICE response use and third-party relay fallback. |
| `third_party/opennow-streamer/crates/opennow-streamer-core/src/nvst_rtsp.rs` | Native sidecar RTSP(S)-derived control WebSocket. |
| `src/client/components/settings/sections/SettingsStreamSection.tsx` | Settings UI's curated CloudMatch region list. |
