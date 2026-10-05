// OpenNOW native client — build configuration.
//
// Everything the C++ client needs to know about *how* it was compiled lives
// here. The values are injected by the build system (CMake / Makefile) and
// default to the portable, dependency-free configuration so the tree also
// compiles when someone just points a compiler at `src/`.
#pragma once

#include <string>

#define ONOW_VERSION_MAJOR 1
#define ONOW_VERSION_MINOR 0
#define ONOW_VERSION_PATCH 0
#define ONOW_VERSION_STRING "1.0.0-native"

// Platform selection. Exactly one of these is defined by the build system.
//   ONOW_PLATFORM_WIN32    — Win32 window + WGL/OpenGL 3 context (no external deps)
//   ONOW_PLATFORM_GLFW     — GLFW window + OpenGL 3 context (Linux/macOS/Windows)
//   ONOW_PLATFORM_HEADLESS — no window at all; drives the app with a synthetic
//                            clock. Used by the unit tests and by CI.
#if !defined(ONOW_PLATFORM_WIN32) && !defined(ONOW_PLATFORM_GLFW) && !defined(ONOW_PLATFORM_HEADLESS)
#if defined(_WIN32)
#define ONOW_PLATFORM_WIN32 1
#else
#define ONOW_PLATFORM_HEADLESS 1
#endif
#endif

// TLS transport. Exactly one.
//   ONOW_TLS_SCHANNEL — Windows SChannel (native, always available)
//   ONOW_TLS_OPENSSL  — OpenSSL, loaded at runtime through dlopen/GetProcAddress
//   ONOW_TLS_NONE     — plain TCP only (offline / debugging)
#if !defined(ONOW_TLS_SCHANNEL) && !defined(ONOW_TLS_OPENSSL) && !defined(ONOW_TLS_NONE)
#if defined(_WIN32)
#define ONOW_TLS_SCHANNEL 1
#else
#define ONOW_TLS_OPENSSL 1
#endif
#endif

// Optional accelerated video decoding. When neither is available the engine
// still runs — it just reports frames as "not decodable on this host" instead
// of painting them, which keeps every other subsystem testable.
#if !defined(ONOW_VIDEO_FFMPEG) && !defined(ONOW_VIDEO_NULL)
#define ONOW_VIDEO_NULL 1
#endif

// Optional audio output.
#if !defined(ONOW_AUDIO_WASAPI) && !defined(ONOW_AUDIO_ALSA) && !defined(ONOW_AUDIO_SDL) && !defined(ONOW_AUDIO_NONE)
#if defined(_WIN32)
#define ONOW_AUDIO_WASAPI 1
#else
#define ONOW_AUDIO_NONE 1
#endif
#endif

namespace onow {

// Human readable build description, shown in Settings → About and `--version`.
std::string build_description();

} // namespace onow
