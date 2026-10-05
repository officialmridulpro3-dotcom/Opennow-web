// NVST transport: the sidecar engine.
//
// This is the C++ equivalent of `src/server/nativeStream.ts` — it spawns the
// vendored Rust streamer, performs the bounded stdio handshake
// (`{"type":"hello","protocolVersion":7}` then `{"type":"start","context":…}`),
// and supervises it for the lifetime of the session. Every wait is bounded
// (hello 30s, start-ack 120s, tunable through OPENNOW_NVST_{HELLO,START}_TIMEOUT_MS)
// exactly like the TypeScript version, so a silent engine surfaces as an error
// instead of an infinite spinner.
#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "onow/Json.h"
#include "onow/stream/Process.h"
#include "onow/stream/StreamEngine.h"

namespace onow {
namespace stream {

// The engine's shortcut bindings, mirrored from the web client's settings.
struct NvstShortcuts {
  std::string toggle_stats = "F3";
  std::string toggle_pointer_lock = "F8";
  std::string toggle_fullscreen = "F10";
  std::string stop_stream = "Ctrl+Shift+Q";
  std::string toggle_anti_afk = "Ctrl+Shift+K";
  std::string toggle_microphone = "Ctrl+Shift+M";
  std::string screenshot = "F11";
  std::string toggle_recording = "F12";
};

// Everything the engine needs beyond the normalized SessionContext: the raw
// SessionInfo, the stream settings, and the shortcut bindings. The engine
// validates these, so they are forwarded verbatim.
struct NvstLaunch {
  SessionContext context;
  Json session;  // SessionInfo
  Json settings; // StreamSettings
  NvstShortcuts shortcuts;
  std::string game_title;
  std::string executable_path; // empty = auto-detect
};

// Live engine state the UI reads each frame.
struct NvstStatus {
  bool running = false;
  bool surface_attached = false;
  bool first_frame = false;
  std::string surface_error;
  std::string last_error;
  std::string phase; // "handshake" | "starting" | ""
  int64_t pid = 0;
  int exit_code = -1;
};

class NvstEngine : public StreamEngine {
public:
  NvstEngine();
  ~NvstEngine() override;

  // Inherited from StreamEngine; NVST sessions must use `launch`.
  bool start(const SessionContext& context) override;
  void stop() override;

  bool launch(const NvstLaunch& launch, std::string& error);

  void send_key(uint32_t key_code, bool down, uint32_t modifiers) override;
  void send_mouse_move(int32_t dx, int32_t dy) override;
  void send_mouse_button(uint32_t button, bool down) override;
  void send_mouse_wheel(int32_t dx, int32_t dy) override;
  void send_gamepad_state(const uint8_t* state, size_t size) override;

  StreamStats stats() const override;
  bool is_connected() const override { return connected_; }
  bool request_keyframe() override;

  // Whitelisted engine commands (the same list the web client forwarded).
  void command(const std::string& type, bool paused = false, bool fullscreen = false);
  void update_surface(int x, int y, int width, int height, bool visible, float device_scale_factor,
                      bool show_stats, const std::string& window_handle);
  void start_recording(const std::string& output_path);
  void stop_recording();

  NvstStatus status() const;

private:
  void stdout_loop();
  void stderr_loop();
  void handle_message(const Json& message);
  bool send_message(const Json& message);
  bool await_ack(bool start_phase, std::string& error);
  std::string resolve_executable(const std::string& configured) const;
  Json build_context_json(const NvstLaunch& launch) const;

  Process process_;
  std::thread stdout_thread_;
  std::thread stderr_thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> connected_{false};

  mutable std::mutex mutex_;
  std::condition_variable ack_cv_;
  bool ack_ready_ = false;
  bool ack_failed_ = false;
  std::string ack_error_;
  int command_counter_ = 0;

  NvstStatus status_;
  StreamStats stats_;
  SessionContext context_;
  std::string recording_start_command_id_;
};

} // namespace stream
} // namespace onow
