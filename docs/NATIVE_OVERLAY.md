# Native stream overlays retired

OpenNOW Desktop no longer creates a transparent Tauri overlay window or renders
a stream deck over native gameplay. Native sessions use a separate maximized
SDL game window, while the opaque Tauri window remains the launcher.

- Ctrl+G / Guide and Ctrl+N are suppressed during native playback.
- F10 toggles fullscreen in the separate game window; the launcher also has a
  minimal fullscreen control that auto-hides when idle.
- The WebRTC player keeps its in-app controls and sidebar when selected.

The former `NativeOverlayRoot` components, Tauri overlay IPC and overlay build
entry were removed. See [NATIVE_STREAMER.md](NATIVE_STREAMER.md) for the current
native-window architecture.
