# Architecture

## Layers

```
┌─────────────────────────────────────────────────────────────────────────┐
│ WinUI 3 XAML (C++/WinRT)                                                 │
│   MainWindow (rail + header + frame + status bar)                        │
│   Views: Login · Home · Library · GameDetails · Stream · Settings · Playtime │
│   Controls: StreamSurfacePanel (SwapChainPanel + DirectX)                │
│   Themes: GeneratedTokens · Styles · Accents · Strings/<lang>            │
└───────────────────────────┬─────────────────────────────────────────────┘
                            │  opennow::app::AppServices (plain C++)
┌───────────────────────────┴─────────────────────────────────────────────┐
│ Application services                                                      │
│   AppServices      settings, session, catalog cache, playtime, regions    │
│   StreamController create → poll → signaling → transport → frames         │
│   IStreamTransport pluggable media plane (libwebrtc / diagnostic)         │
└───────────────────────────┬─────────────────────────────────────────────┘
                            │  opennow::core (C++20, no WinRT)
┌───────────────────────────┴─────────────────────────────────────────────┐
│ Core                                                                      │
│   gfn::AuthService · CatalogService · CloudMatchService ·                 │
│   SubscriptionService · SignalingClient · DeviceId                        │
│   Json · Hash · Http (WinHTTP) · Settings (DPAPI) · Logging · Paths        │
└───────────────────────────┬─────────────────────────────────────────────┘
                            │
┌───────────────────────────┴─────────────────────────────────────────────┐
│ DirectX                                                                   │
│   StreamSurface: ID3D10Device1 renderer ──shared texture──▶ ID3D11 (FL10) │
│                  composition swap chain ──▶ SwapChainPanel                │
└─────────────────────────────────────────────────────────────────────────┘
```

## Why the Core is WinRT-free

Every service in `src/Core` is a plain C++ class that talks to an injected
`IHttpClient`. Two payoffs:

1. The protocol code — the part most likely to break when NVIDIA changes a
   field — is unit tested on Linux with a scripted client (95 assertions).
2. The UI layer can call it from `winrt::resume_background()` coroutines
   without marshalling WinRT objects across threads.

## Threading

| Thread | Work |
|---|---|
| UI (XAML) | binding, input, DirectX present, frame hand-off |
| `StreamController` worker | CloudMatch create/poll loop, signaling start |
| transport thread | decode + `onFrame` callbacks |
| latency probe pool | one bounded HTTPS request per region, detached |

Callbacks into the UI always go through `DispatcherQueue().TryEnqueue`. Frames
are moved (not copied) into the dispatcher lambda.

## DirectX 10 rendering path

```
decoded NV12/I420/BGRA (transport)
        │  SubmitFrame: Map/Unmap into a D3D10_USAGE_DYNAMIC staging texture
        ▼
ID3D10Device1  (feature level 10_1, falling back to 10_0)
        │  vs_4_0 / ps_4_0 shaders: YUV→RGB matrix, sharpening
        ▼
shared BGRA8 render target  (D3D10_RESOURCE_MISC_SHARED_KEYEDMUTEX)
        │  IDXGIKeyedMutex Acquire(0)/Release(0) on each side
        ▼
ID3D11Device @ feature level 10_1  (composition only — no D3D11 features used)
        │  letterbox copy shader (vs_4_0 / ps_4_0)
        ▼
IDXGISwapChain1 (flip-sequential, composition) → ISwapChainPanelNative
        ▼
XAML tree composites it with the HUD, overlays and controls
```

Two deliberate choices:

- **The renderer is a real D3D10 device, not a D3D11 device at FL 10.** All
  video work (upload, colour conversion, sharpening) is shader model 4.0 on
  `ID3D10Device1`. That is the GPU tier we promise.
- **The composition device is D3D11 at feature level 10_1.** XAML will only
  composite a swap chain created against a D3D11 device. The D3D11 runtime runs
  on every DirectX 10 GPU, and this file uses no D3D11-only feature, so the
  promise holds while the XAML requirement is met.

Shaders are compiled at runtime with `D3DCompile` (d3dcompiler_47.dll ships
with Windows), so there is no FXC build step and no compiled shader blobs to
keep in sync.

## State and persistence

| Data | Location | Protection |
|---|---|---|
| Settings | `%LOCALAPPDATA%\OpenNOW\settings.json` | plain JSON, user editable |
| Credentials | `%LOCALAPPDATA%\OpenNOW\account.token` | `CryptProtectData` (DPAPI, per Windows account) |
| Playtime | `%LOCALAPPDATA%\OpenNOW\playtime.json` | plain JSON |
| Catalog cache | `%LOCALAPPDATA%\OpenNOW\cache\<sha256>.json` | plain JSON |
| Logs | `%LOCALAPPDATA%\OpenNOW\logs\opennow.log` | rotated at 4 MB, secrets redacted |

## Keeping the two clients in sync

Three generators read the web sources and emit native files:

| Generator | Source | Output |
|---|---|---|
| `sync-design-tokens.mjs` | `src/client/styles.css` `:root` (incl. `color-mix()`) | `Themes/GeneratedTokens.xaml` |
| `extract-graphql-queries.mjs` | `src/server/gfn/lcarsGraphql.ts` | `gfn/GeneratedQueries.inc` |
| `sync-locales.mjs` | `locales/*.json` | `Strings/<lang>/Resources.resw` |

Wire this into CI: fail the build when a generator's output differs from what is
committed, and the native client can never silently drift from the web client.
