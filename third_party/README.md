# Vendored third-party code

## `opennow-streamer/` — native NVST streaming engine

- **Source:** <https://github.com/OpenCloudGaming/OpenNOW>
- **Pinned ref:** `v1.0.1` (`native/opennow-streamer/` subtree only)
- **License:** MIT (same as this repo)
- **Vendored:** 2026-09-21, unmodified

This is upstream OpenNOW's native GeForce NOW streaming engine (RTSPS + Mjolnir
SRTP video + ICE/DTLS/SCTP audio/input — no WebRTC, no NVIDIA binaries). The
desktop shell spawns its standalone binary (`opennow-streamer`) as the
`opennow-nvst` sidecar for native-protocol gameplay. See
[docs/NATIVE_STREAMER.md](../docs/NATIVE_STREAMER.md) for the integration plan.

Only the streamer workspace is vendored — `opennow-core` (account services)
and `opennow-qt` (Qt shell) are intentionally excluded: auth, catalog, and
CloudMatch allocation stay in this repo's Node backend, which feeds the engine
a `SessionContext` JSON per stream.

### Updating the pin

```bash
# In a scratch clone of upstream:
git checkout <new-tag>
cp -r native/opennow-streamer <this-repo>/third_party/opennow-streamer
# Then update the "Pinned ref" line above and the FFI/protocol notes in
# docs/NATIVE_STREAMER.md (protocol version, SessionContext shape).
```

Keep the vendored tree byte-identical to upstream so updates stay trivial.
Any OpenNOW-web-specific changes belong in our own shell/backend code, never
as edits inside `third_party/` — with one documented exception.

### Local patches (apply after re-copying the tree)

| File | Change | Reason |
|---|---|---|
| `crates/opennow-streamer-platform/src/output.rs` | `update()` does not mark the surface embedded while `visible=false`; standalone window stays as a fallback | black-screen fix (missing Tauri HWND during startup) |
| `crates/opennow-streamer-core/src/lib.rs` | `host_overlay_enabled()` (env `OPENNOW_NATIVE_HOST_OVERLAY`) forwards Ctrl+G / Ctrl+N / Alt+Enter / pointer-lock, and the Guide button, to the host instead of opening the built-in GDI panel | the host shell draws the stream chrome in a styled transparent web view — see [docs/NATIVE_OVERLAY.md](../docs/NATIVE_OVERLAY.md). Unset ⇒ upstream behaviour, built-in panel in charge |

Both patches are additive and env-/state-gated, so re-applying them after an
upstream bump is a two-hunk diff.
