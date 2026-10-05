// The streaming engine: what the SessionContext is and how the two transports
// differ.
//
// Two completely separate code paths live here, mirroring the web client:
//
//   * WebRTC (settings.transport_mode = "webrtc"). The browser did all the
//     work through RTCPeerConnection; here we speak the SDP/ICE exchange over
//     the backend's signaling WebSocket ourselves, feed the remote description
//     into the platform's peer-connection implementation, and receive decoded
//     frames from it.
//
//   * NVST (settings.transport_mode = "nvst"). This is the original GeForce
//     NOW protocol the vendored Rust streamer implements: RTSPS over TLS for
//     the control channel, SRTP over UDP for the media, and an SCTP data
//     channel for input. It is driven by the SessionContext JSON described in
//     docs/NATIVE_STREAMER.md and spoken over stdio by the child process.
//
// Both paths converge on `StreamEngine::receive_frame`, which hands RGBA
// frames to whoever owns the surface.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "onow/Json.h"

namespace onow {
namespace stream {

// Video frame handed to the renderer. Stride is bytes per row; width*4 for
// RGBA8.
struct VideoFrame {
  std::vector<uint8_t> pixels;
  int width = 0;
  int height = 0;
  int stride = 0;
  uint64_t timestamp_us = 0; // capture time, for jitter stats
  uint64_t frame_index = 0;
};

// Aggregate transport statistics, refreshed by the engine and read by the UI.
struct StreamStats {
  uint64_t frames_received = 0;
  uint64_t bytes_received = 0;
  uint64_t packets_lost = 0;
  uint64_t packets_received = 0;
  double rtt_ms = 0.0;
  double jitter_ms = 0.0;
  double bitrate_mbps = 0.0;
  double decode_ms = 0.0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t fps = 0;
  bool connected = false;
};

enum class StreamEvent {
  Connected,
  Disconnected,
  Failed,
  FirstFrame,
};

using FrameCallback = std::function<void(const VideoFrame&)>;
using EventCallback = std::function<void(StreamEvent, const std::string&)>;

// SessionContext — the exact JSON contract the native streamer speaks.
// See docs/NATIVE_STREAMER.md in the repository root.
struct SessionContext {
  // Session identity.
  std::string session_id;
  std::string app_id;
  std::string zone;
  std::string region;

  // Where to connect.
  std::string rtsps_endpoint;      // rtsp+https://host:port/path
  std::string server_certificate;  // pinned server cert fingerprint
  std::string client_id;
  std::string signaling_url;

  // ICE, for the WebRTC path.
  std::vector<std::string> ice_servers;

  // Negotiated stream profile.
  int width = 1920;
  int height = 1080;
  int fps = 60;
  int bitrate_kbps = 75000;
  std::string codec = "H264";
  std::string audio_codec = "OPUS";

  // Input channel.
  std::string input_endpoint;      // host:port for the SRTP/SCTP channel
  bool mouse_relative_mode = true;
  bool controller_support = true;

  // Auth material for the RTSPS handshake.
  std::string auth_token;
  std::string session_key;

  Json to_json() const;
  static SessionContext from_json(const Json& json);
};

class StreamEngine {
public:
  virtual ~StreamEngine() = default;

  virtual bool start(const SessionContext& context) = 0;
  virtual void stop() = 0;

  // Input. The engine queues and forwards these; the transport decides how
  // (SCTP message, data channel, or NVST input packet).
  virtual void send_key(uint32_t key_code, bool down, uint32_t modifiers) = 0;
  virtual void send_mouse_move(int32_t dx, int32_t dy) = 0;
  virtual void send_mouse_button(uint32_t button, bool down) = 0;
  virtual void send_mouse_wheel(int32_t dx, int32_t dy) = 0;
  virtual void send_gamepad_state(const uint8_t* state, size_t size) = 0;

  virtual StreamStats stats() const = 0;
  virtual bool is_connected() const = 0;
  virtual bool request_keyframe() = 0;

  void set_frame_callback(FrameCallback cb) { frame_cb_ = std::move(cb); }
  void set_event_callback(EventCallback cb) { event_cb_ = std::move(cb); }

protected:
  void emit_frame(const VideoFrame& frame) {
    if (frame_cb_) frame_cb_(frame);
  }
  void emit_event(StreamEvent event, const std::string& detail = std::string()) {
    if (event_cb_) event_cb_(event, detail);
  }

  FrameCallback frame_cb_;
  EventCallback event_cb_;
};

// Input event, used by both transports and by the recorder.
struct InputEvent {
  enum class Kind { Key, MouseMove, MouseButton, MouseWheel, Gamepad, Touch };
  Kind kind = Kind::Key;
  uint64_t timestamp_us = 0;
  uint32_t code = 0;
  int32_t dx = 0;
  int32_t dy = 0;
  bool down = false;
  std::vector<uint8_t> payload;
};

} // namespace stream
} // namespace onow
