// See NvstEngine.h for the protocol and the bounded-wait policy.
#include "onow/stream/NvstEngine.h"

#include "onow/Log.h"
#include "onow/Str.h"
#include "onow/Time.h"

#include <chrono>
#include <cstdlib>

namespace onow {
namespace stream {

namespace {

constexpr int kEngineProtocolVersion = 7;

// stderr markers, copied from src/server/nativeStream.ts.
const char* kFirstFrameMarkers[] = {"inbound first datagram", "first H264 access unit",
                                    "first H265 access unit", "first AV1 access unit"};
const char* kSurfaceAttachedMarker = "External SDL surface attached";
const char* kSurfaceFailureMarkers[] = {"External SDL surface attach failed",
                                        "external SDL surface: visible rect but no window handle",
                                        "shell-placement: standalone window suppressed"};

bool contains_any(const std::string& haystack, const char* const* needles, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (str_contains(haystack, needles[i])) return true;
  }
  return false;
}

int env_timeout_ms(const char* name, int fallback_ms) {
  const char* raw = std::getenv(name);
  if (!raw || !*raw) return fallback_ms;
  long long parsed = 0;
  if (!str_to_ll(raw, parsed) || parsed <= 0) return fallback_ms;
  return static_cast<int>(parsed);
}

} // namespace

NvstEngine::NvstEngine() = default;

NvstEngine::~NvstEngine() { stop(); }

bool NvstEngine::start(const SessionContext& context) {
  // NVST sessions must go through launch(); refusing here keeps a caller from
  // accidentally running the NVST engine with a WebRTC-shaped context.
  context_ = context;
  return false;
}

void NvstEngine::stop() {
  if (!running_) return;
  running_ = false;
  connected_ = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.running = false;
    status_.phase.clear();
  }
  ack_cv_.notify_all();
  process_.terminate(3000);
  if (stdout_thread_.joinable()) stdout_thread_.join();
  if (stderr_thread_.joinable()) stderr_thread_.join();
  emit_event(StreamEvent::Disconnected, "native session stopped");
}

std::string NvstEngine::resolve_executable(const std::string& configured) const {
  if (!configured.empty()) return configured;
  if (const char* env = std::getenv("OPENNOW_NVST_SIDECAR")) {
    if (env && *env) return std::string(env);
  }
#if defined(_WIN32)
  return "opennow-streamer.exe";
#elif defined(__APPLE__)
  return "opennow-streamer";
#else
  return "opennow-streamer";
#endif
}

Json NvstEngine::build_context_json(const NvstLaunch& launch) const {
  Json context = Json::object();
  context["session"] = launch.session;
  context["settings"] = launch.settings;
  Json shortcuts = Json::object();
  shortcuts["toggleStats"] = Json(launch.shortcuts.toggle_stats);
  shortcuts["togglePointerLock"] = Json(launch.shortcuts.toggle_pointer_lock);
  shortcuts["toggleFullscreen"] = Json(launch.shortcuts.toggle_fullscreen);
  shortcuts["stopStream"] = Json(launch.shortcuts.stop_stream);
  shortcuts["toggleAntiAfk"] = Json(launch.shortcuts.toggle_anti_afk);
  shortcuts["toggleMicrophone"] = Json(launch.shortcuts.toggle_microphone);
  shortcuts["screenshot"] = Json(launch.shortcuts.screenshot);
  shortcuts["toggleRecording"] = Json(launch.shortcuts.toggle_recording);
  context["shortcuts"] = shortcuts;
  // The normalized view, so a debugging session can see the resolved media
  // endpoints alongside the raw session.
  context["resolved"] = launch.context.to_json();
  return context;
}

bool NvstEngine::launch(const NvstLaunch& launch, std::string& error) {
  if (running_) {
    error = "A native stream is already running.";
    return false;
  }
  context_ = launch.context;

  const std::string executable = resolve_executable(launch.executable_path);
  std::vector<std::string> env;
  if (!launch.game_title.empty()) env.push_back("OPENNOW_GAME_TITLE=" + launch.game_title);
  if (const char* existing = std::getenv("OPENNOW_NVST_SIDECAR")) {
    if (existing && *existing) env.push_back(std::string("OPENNOW_NVST_SIDECAR=") + existing);
  }

  if (!process_.start(executable, {}, env, error)) {
    error = "Could not start the native streamer (" + executable + "): " + error;
    return false;
  }

  running_ = true;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = NvstStatus{};
    status_.running = true;
    status_.pid = 0;
    status_.phase = "handshake";
  }
  stdout_thread_ = std::thread([this] { stdout_loop(); });
  stderr_thread_ = std::thread([this] { stderr_loop(); });

  // hello
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ack_ready_ = false;
    ack_failed_ = false;
    ack_error_.clear();
  }
  Json hello = Json::object();
  hello["id"] = Json("hello-1");
  hello["type"] = Json("hello");
  hello["protocolVersion"] = Json(static_cast<double>(kEngineProtocolVersion));
  if (!send_message(hello)) {
    error = "The native streamer's input is closed.";
    stop();
    return false;
  }
  if (!await_ack(false, error)) {
    stop();
    return false;
  }

  // start
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ack_ready_ = false;
    ack_failed_ = false;
    ack_error_.clear();
    status_.phase = "starting";
  }
  Json start = Json::object();
  start["id"] = Json("start-2");
  start["type"] = Json("start");
  start["context"] = build_context_json(launch);
  if (!send_message(start)) {
    error = "The native streamer's input is closed.";
    stop();
    return false;
  }
  if (!await_ack(true, error)) {
    stop();
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.phase.clear();
  }
  connected_ = true;
  emit_event(StreamEvent::Connected);
  return true;
}

bool NvstEngine::await_ack(bool start_phase, std::string& error) {
  const int timeout_ms = env_timeout_ms(start_phase ? "OPENNOW_NVST_START_TIMEOUT_MS"
                                                    : "OPENNOW_NVST_HELLO_TIMEOUT_MS",
                                        start_phase ? 120000 : 30000);
  std::unique_lock<std::mutex> lock(mutex_);
  const bool ok = ack_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                   [this] { return ack_ready_ || ack_failed_ || !running_; });
  if (ack_failed_) {
    error = ack_error_.empty() ? "The native streamer rejected the session." : ack_error_;
    return false;
  }
  if (!ok || !running_) {
    error = "The native streamer did not answer the " +
            std::string(start_phase ? "start" : "hello") + " handshake within " +
            std::to_string(timeout_ms / 1000) +
            "s — it may be stuck probing this GPU or reaching the game server.";
    return false;
  }
  return true;
}

bool NvstEngine::send_message(const Json& message) {
  return process_.write_line(0, message.dump());
}

void NvstEngine::stdout_loop() {
  for (;;) {
    std::string line;
    bool eof = false;
    if (!process_.read_line(1, line, 200, eof)) {
      if (eof || !running_) return;
      continue;
    }
    const std::string trimmed = str_trim(line);
    if (trimmed.empty()) continue;
    Json message = Json::parse_or(trimmed);
    if (message.is_null()) continue;
    handle_message(message);
  }
}

void NvstEngine::stderr_loop() {
  for (;;) {
    std::string line;
    bool eof = false;
    if (!process_.read_line(2, line, 200, eof)) {
      if (eof || !running_) return;
      continue;
    }
    const std::string text = str_trim(line);
    if (text.empty()) continue;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!status_.first_frame && contains_any(text, kFirstFrameMarkers, 4)) {
      status_.first_frame = true;
      emit_event(StreamEvent::FirstFrame, text);
    }
    if (str_contains(text, kSurfaceAttachedMarker)) {
      status_.surface_attached = true;
      status_.surface_error.clear();
    } else if (contains_any(text, kSurfaceFailureMarkers, 3)) {
      status_.surface_attached = false;
      status_.surface_error = text;
    }
    ONOW_DEBUG("NVST", "%s", text.c_str());
  }
}

void NvstEngine::handle_message(const Json& message) {
  const std::string type = message.find("type")->as_string();

  if (type == "ready") {
    std::lock_guard<std::mutex> lock(mutex_);
    ack_ready_ = true;
    ack_cv_.notify_all();
    return;
  }
  if (type == "ack") {
    // The engine acknowledges `start`; treat both shapes as success.
    std::lock_guard<std::mutex> lock(mutex_);
    ack_ready_ = true;
    ack_cv_.notify_all();
    return;
  }
  if (type == "telemetry") {
    const Json* fps = message.find("framesPerSecond");
    const Json* bitrate = message.find("bitrateMbps");
    const Json* ping = message.find("pingMs");
    const Json* loss = message.find("packetLossPercent");
    const Json* jitter = message.find("jitterMs");
    const Json* decoded = message.find("framesDecoded");
    const Json* dropped = message.find("framesDropped");
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.fps = static_cast<uint32_t>(fps->as_number(0));
    stats_.bitrate_mbps = bitrate->as_number(0);
    stats_.rtt_ms = ping->as_number(0);
    stats_.packets_lost = static_cast<uint64_t>(loss->as_number(0));
    stats_.jitter_ms = jitter->as_number(0);
    stats_.frames_received = static_cast<uint64_t>(decoded->as_number(0));
    stats_.packets_lost = static_cast<uint64_t>(dropped->as_number(0));
    stats_.width = static_cast<uint32_t>(context_.width);
    stats_.height = static_cast<uint32_t>(context_.height);
    stats_.connected = true;
    if (!status_.first_frame && stats_.frames_received > 0) status_.first_frame = true;
    return;
  }
  if (type == "error") {
    const std::string detail = message.find("message")->as_string("native streamer error");
    {
      std::lock_guard<std::mutex> lock(mutex_);
      status_.last_error = detail;
      ack_failed_ = true;
      ack_error_ = detail;
      if (message.find("id")->as_string() == recording_start_command_id_) {
        recording_start_command_id_.clear();
      }
    }
    ack_cv_.notify_all();
    emit_event(StreamEvent::Failed, detail);
    return;
  }
  if (type == "overlay-request") {
    emit_event(StreamEvent::Connected, "overlay-request");
    return;
  }
  if (type == "shortcut-action") {
    const std::string action = message.find("action")->as_string();
    if (action == "stop-stream") {
      stop();
      return;
    }
    emit_event(StreamEvent::Connected, "shortcut:" + action);
    return;
  }
  if (type == "fullscreen-state") {
    emit_event(StreamEvent::Connected, "fullscreen");
    return;
  }
  if (type == "recording-state") {
    emit_event(StreamEvent::Connected, "recording");
    return;
  }
}

void NvstEngine::send_key(uint32_t key_code, bool down, uint32_t modifiers) {
  Json packet = Json::object();
  packet["type"] = Json("input");
  packet["kind"] = Json("key");
  packet["code"] = Json(static_cast<double>(key_code));
  packet["down"] = Json(down);
  packet["modifiers"] = Json(static_cast<double>(modifiers));
  packet["timestampUs"] = Json(static_cast<double>(now_us()));
  send_message(packet);
}

void NvstEngine::send_mouse_move(int32_t dx, int32_t dy) {
  Json packet = Json::object();
  packet["type"] = Json("input");
  packet["kind"] = Json("mouse-move");
  packet["dx"] = Json(static_cast<double>(dx));
  packet["dy"] = Json(static_cast<double>(dy));
  send_message(packet);
}

void NvstEngine::send_mouse_button(uint32_t button, bool down) {
  Json packet = Json::object();
  packet["type"] = Json("input");
  packet["kind"] = Json("mouse-button");
  packet["button"] = Json(static_cast<double>(button));
  packet["down"] = Json(down);
  send_message(packet);
}

void NvstEngine::send_mouse_wheel(int32_t dx, int32_t dy) {
  Json packet = Json::object();
  packet["type"] = Json("input");
  packet["kind"] = Json("mouse-wheel");
  packet["dx"] = Json(static_cast<double>(dx));
  packet["dy"] = Json(static_cast<double>(dy));
  send_message(packet);
}

void NvstEngine::send_gamepad_state(const uint8_t* state, size_t size) {
  Json packet = Json::object();
  packet["type"] = Json("input");
  packet["kind"] = Json("gamepad");
  packet["size"] = Json(static_cast<double>(size));
  (void)state;
  send_message(packet);
}

StreamStats NvstEngine::stats() const {
  std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
  StreamStats copy = stats_;
  copy.connected = connected_;
  return copy;
}

bool NvstEngine::request_keyframe() {
  // The engine decides its own recovery; the client only logs the request.
  ONOW_DEBUG("NVST", "keyframe requested");
  return true;
}

void NvstEngine::command(const std::string& type, bool paused, bool fullscreen) {
  Json message = Json::object();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++command_counter_;
    message["id"] = Json(type + "-" + std::to_string(command_counter_));
  }
  message["type"] = Json(type);
  if (paused) message["paused"] = Json(true);
  if (fullscreen) message["fullscreen"] = Json(true);
  send_message(message);
}

void NvstEngine::update_surface(int x, int y, int width, int height, bool visible,
                                float device_scale_factor, bool show_stats,
                                const std::string& window_handle) {
  Json message = Json::object();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++command_counter_;
    message["id"] = Json("surface-" + std::to_string(command_counter_));
  }
  message["type"] = Json("surface");
  Json surface = Json::object();
  Json rect = Json::object();
  rect["x"] = Json(static_cast<double>(x));
  rect["y"] = Json(static_cast<double>(y));
  rect["width"] = Json(static_cast<double>(width));
  rect["height"] = Json(static_cast<double>(height));
  surface["rect"] = rect;
  surface["visible"] = Json(visible);
  surface["deviceScaleFactor"] = Json(static_cast<double>(device_scale_factor));
  surface["showStats"] = Json(show_stats);
  surface["windowHandle"] = Json(window_handle);
  surface["screenRect"] = rect;
  message["surface"] = surface;
  send_message(message);
}

void NvstEngine::start_recording(const std::string& output_path) {
  Json message = Json::object();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++command_counter_;
    recording_start_command_id_ = "recording-start-" + std::to_string(command_counter_);
    message["id"] = Json(recording_start_command_id_);
  }
  message["type"] = Json("recording-start");
  message["outputPath"] = Json(output_path);
  send_message(message);
}

void NvstEngine::stop_recording() {
  Json message = Json::object();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++command_counter_;
    message["id"] = Json("recording-stop-" + std::to_string(command_counter_));
    recording_start_command_id_.clear();
  }
  message["type"] = Json("recording-stop");
  send_message(message);
}

NvstStatus NvstEngine::status() const {
  std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
  return status_;
}

} // namespace stream
} // namespace onow
