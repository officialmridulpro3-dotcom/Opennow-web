# Building OpenNOW Native

## Requirements

- **Windows 10 1809 (build 17763) or newer.** That is the Windows App SDK
  1.6 floor; the app itself only needs DirectX 10, so older GPUs are fine as
  long as the OS is current.
- **Visual Studio 2022 (17.11 or newer)** with:
  - *Desktop development with C++*
  - *Windows application development* (adds C++/WinRT and the Windows App SDK)
  - Windows 10/11 SDK **10.0.22621.0** (or change `TargetPlatformVersion` in
    `src/App/OpenNOW.App.vcxproj` to what you have installed).
- **NuGet** package restore (Visual Studio does this automatically).

No vcpkg is required for a default build: JSON, Base64, MD5, SHA-256, HTTP and
WebSockets are all implemented in-tree against Win32/WinRT.

## Build and run

```powershell
# from the repository root
msbuild native\OpenNOW.sln /p:Configuration=Release /p:Platform=x64
start native\build\x64\Release\OpenNOW.App.exe
```

Or open `native/OpenNOW.sln` in Visual Studio, pick `x64`, and press F5. The
app is **unpackaged** (`WindowsPackageType=None`), so it runs straight from the
output folder without installation.

### Debug build notes

- Debug builds enable `D3D10_CREATE_DEVICE_DEBUG` and `D3D11_CREATE_DEVICE_DEBUG`.
  If device creation fails on a machine without the Graphics Tools optional
  feature, remove those flags or install *Graphics Tools* in Windows Settings.

## Testing the Core on Linux or macOS CI

The Core library has no platform dependencies, so the protocol code is tested
without Windows:

```bash
cmake -S native/src/Core -B /tmp/opennow-core -DOPENNOW_BUILD_TESTS=ON
cmake --build /tmp/opennow-core -j
ctest --test-dir /tmp/opennow-core --output-on-failure
```

As a one-liner without CMake:

```bash
cd native/src/Core
g++ -std=c++20 -Iinclude -Wall -Wextra -o /tmp/opennow-core-tests \
    tests/test_core.cpp src/*.cpp src/gfn/*.cpp && /tmp/opennow-core-tests
```

Expected: `95/95 checks passed`. The tests use a scripted `IHttpClient`, so they
never touch NVIDIA's servers.

## Regenerating the synced files

Three files are generated from the web client so the two clients cannot drift.
Re-run them after changing the web side:

```bash
node native/tools/sync-design-tokens.mjs    # src/client/styles.css -> Themes/GeneratedTokens.xaml
node native/tools/extract-graphql-queries.mjs  # LCARS query text -> GeneratedQueries.inc
node native/tools/sync-locales.mjs          # locales/*.json -> Strings/<lang>/Resources.resw
```

## Linking the production media transport (libwebrtc)

The default build ships the **diagnostic transport**: it completes a real
CloudMatch session and renders a colour-bars pattern through the DirectX
pipeline, which is enough to validate queueing, signaling, the HUD and session
teardown without a stream.

For real play, provide `IStreamTransport` backed by libwebrtc:

1. Build or download a libwebrtc static library (the same revision the web
   client's peer connection would use).
2. Add `native/src/App/Services/WebRtcTransport.cpp` implementing
   `opennow::app::IStreamTransport` — create a `PeerConnection`, feed the seat's
   SDP offer, answer, and forward `OnFrame` callbacks as NV12.
3. Return it from `CreateStreamTransport()` in `AppServices.cpp` when
   `OPENNOW_HAVE_LIBWEBRTC` is defined, and add the include/lib paths to
   `OpenNOW.App.vcxproj`.

Nothing else in the tree depends on the transport, so this is a single-file
change.

## Updating the Windows App SDK

`Microsoft.WindowsAppSDK` and `Microsoft.Windows.ImplementationLibrary`
versions are declared in `src/App/OpenNOW.App.vcxproj`;
`Directory.Build.props` carries a matching `<WindowsAppSDKVersion>` for
reference. Bump both together.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `No DirectX 10 device could be created` | driver too old or Running in a VM/remote session | Update the GPU driver; on a VM enable GPU passthrough. The app still starts and shows this in *Settings → About*. |
| Stream panel is black | swap chain not attached | Confirm `ISwapChainPanelNative` query succeeded (`opennow.log` in `%LOCALAPPDATA%\OpenNOW\logs`). |
| Sign-in page shows "········" forever | provider discovery blocked | Corporate proxy/DNS block on `pcs.geforcenow.com`. The client falls back to the NVIDIA provider automatically after the request times out. |
| HTTP 400 on catalog | NVIDIA rotated persisted query hashes | The client retries with the inline query text automatically; if it still fails, run `extract-graphql-queries.mjs` against a newer `lcarsGraphql.ts`. |

Logs: `%LOCALAPPDATA%\OpenNOW\logs\opennow.log` (rotate at 4 MB). Tokens are
never written to the log — they are registered with the logger as redacted
secrets.
