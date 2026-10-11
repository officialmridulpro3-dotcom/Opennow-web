# Porting status

Ported from `src/client/**` (React) and `src/server/gfn/**` (Node) into
`native/**`. Status key: **done** = implemented and exercised; **stub** =
interface and wiring exist, implementation is a clearly-marked placeholder;
**dropped** = intentionally not ported, with the reason.

## Core services — done

| Web source | Native | Notes |
|---|---|---|
| `gfn/auth/deviceLogin.ts` | `gfn/Auth.cpp` | device authorize, token poll, pending vs fatal errors |
| `gfn/auth/tokenRefresh.ts` | `gfn/Auth.cpp` | refresh rotation, client tokens |
| `gfn/auth/userInfo.ts` | `gfn/Auth.cpp` | JWT claims, userinfo, Gravatar |
| `webAuth.ts` provider discovery | `gfn/Auth.cpp` | priority sort, BPC → bro.game rename, slash normalisation, default fallback |
| `gfn/lcarsGraphql.ts` | `gfn/Catalog.cpp` | persisted hashes + inline-query retry on HTTP 400 |
| `gfn/catalogBrowse.ts` | `gfn/Catalog.cpp` | panels → shelves, browse/search paging |
| `gfn/gameAppMapper.ts` | `gfn/Catalog.cpp` | app → game mapping, image width rewriting |
| `gfn/cloudmatchSessionRequest.ts` | `gfn/CloudMatch.cpp` | create + claim bodies, WebRTC/NVST metadata split |
| `gfn/cloudmatchSessionParsing.ts` | `gfn/CloudMatch.cpp` | queue position, seat setup, ads, ICE, SDP |
| `gfn/cloudmatchSignaling.ts` | `gfn/Signaling.cpp` | `resolveSignaling`, rtsps→wss URL building |
| `gfn/signaling.ts` | `gfn/Signaling.cpp` | peer_info / SDP / ICE framing |
| `gfn/subscription.ts` | `gfn/Subscription.cpp` | MES tier, dynamic regions, latency probe |
| `sessionStore.ts` / `appData.ts` | `Settings.cpp`, `Paths.cpp`, `Hash.cpp` | JSON settings, DPAPI secrets, cache, logs |

Wire compatibility is byte-level where it matters: unit tests assert the exact
CloudMatch create body, the claim body's refusal to renegotiate, the rtsps→wss
signaling URL, and the `peer_info` framing.

## UI — done

| Web view | Native page |
|---|---|
| `Navbar.tsx` + `SideRail.tsx` + `StatusBar.tsx` | `MainWindow.xaml` |
| `LoginScreen.tsx` | `Views/LoginPage` |
| `HomePage.tsx` | `Views/HomePage` (shelves + spotlight) |
| `LibraryPage.tsx` | `Views/LibraryPage` (grid, filters, search, fade-in) |
| `GameDetailsPage.tsx` | `Views/GameDetailsPage` (hero, facts, region picker) |
| `StreamView.tsx` | `Views/StreamPage` (DirectX surface, queue overlay, HUD, controls) |
| `SettingsPage.tsx` + 8 sections | `Views/SettingsPage` |
| `PlaytimePage.tsx` | `Views/PlaytimePage` |
| `styles.css` design tokens | `Themes/GeneratedTokens.xaml` (generated) |
| — | `Themes/Styles.xaml`, `Themes/Accents.xaml` |
| `locales/*.json` | `Strings/<lang>/Resources.resw` (generated) |

## Stubs and known gaps

| Area | Status | What is missing |
|---|---|---|
| Media transport | **stub** | `IStreamTransport` is implemented by a diagnostic colour-bars source. libwebrtc is required for real play — see BUILD.md; it is a single-file change. |
| QR code on the sign-in page | **not ported** | The web client shows a QR of the verification URL. The native page offers the pre-filled link and a copy button instead. A small QR encoder in Core would restore parity. |
| Native (NVST) streamer sidecar | **dropped** | The web client's NVST sidecar exists because a browser cannot open RTSPS. A native client does not need a sidecar — an NVST transport would live behind the same `IStreamTransport`. |
| Input injection (keyboard scancodes, mouse, XInput) | **not ported** | `InputSettings` is persisted and exposed in Settings, but the transport must carry it to the seat. Depends on the transport work above. |
| Discord presence | **dropped** | Was Electron/main-process only; no equivalent needed and no dependency wanted. |
| App updater | **dropped** | Web/desktop-only concern; ship an MSIX or an installer instead. |
| Local recording / screenshots | **not ported** | `ScreenshotDirectory()` exists; capture would read back the shared texture. |
| Ads during queue | **partial** | Ad state is parsed and reported to the UI, but the media is not played. |
| `x:Bind` vs `Binding` | design choice | Pages use classic `{Binding}` to keep the markup readable; `{x:Bind}` would compile-time check the view models. |

## Verification performed in this repository

- `native/src/Core` builds clean under g++ 12 with `-Wall -Wextra`.
- 95 assertions pass, covering JSON (UTF-8, astral plane, malformed input,
  depth limits), MD5/SHA-256 vectors, Base64/Base64url round trips, the
  CloudMatch create and claim bodies, poll parsing, signaling URL construction,
  the device-login flow (pending → success), JWT parsing, catalog mapping, and
  settings/playtime round trips.
- `sync-design-tokens.mjs`, `extract-graphql-queries.mjs` and
  `sync-locales.mjs` were run; the generated XAML, C++ and `.resw` files parse
  as valid XML/C++ include and are committed.

**Not verified here:** the Windows build. This workspace is Linux, so MSVC,
C++/WinRT code generation (MIDL/`.idl`), and the DirectX paths could not be
compiled or run. Expect to fix build-level issues (include order, WinRT
namespace details, SDK version numbers) on first compile in Visual Studio; the
logic underneath is tested.
