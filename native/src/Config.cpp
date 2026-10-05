#include "onow/Config.h"

#include "onow/platform/Platform.h"

namespace onow {

std::string build_description() {
  std::string out;
#if defined(ONOW_PLATFORM_WIN32)
  out += "win32";
#elif defined(ONOW_PLATFORM_GLFW)
  out += "glfw";
#elif defined(ONOW_PLATFORM_HEADLESS)
  out += "headless";
#else
  out += "unknown";
#endif

  out += " / ";
#if defined(ONOW_TLS_SCHANNEL)
  out += "schannel";
#elif defined(ONOW_TLS_OPENSSL)
  out += "openssl";
#else
  out += "no-tls";
#endif

  out += " / ";
#if defined(ONOW_VIDEO_FFMPEG)
  out += "ffmpeg";
#else
  out += "no-video-accel";
#endif
  return out;
}

} // namespace onow
