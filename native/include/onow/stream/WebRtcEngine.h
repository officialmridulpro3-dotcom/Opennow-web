// WebRTC transport.
//
// Everything up to and including the signaling handshake is real: this opens
// the backend's `/api/signaling` socket, sends `connect`, receives the peer's
// `offer`, sends our `answer`, and exchanges ICE candidates — exactly the
// frames `src/client/api.ts` sends (types `connect` / `answer` / `ice` /
// `keyframe`, inbound frames are `{type:"event", payload:…}`).
//
// What is deliberately *not* implemented here is the media stack itself:
// DTLS key exchange, SRTP decryption, RTP depacketisation and H.264 decoding.
// A browser does that inside RTCPeerConnection, and the vendored Rust streamer
// does it inside GStreamer; reimplementing both from scratch in this port
// would double the code for no behavioural gain. Instead the engine defines
// the seam where that work belongs (`VideoDecoder` and `RtpReceiver`) so a
// platform backend can plug in WebRTC's own library or a hardware decoder
// without touching the UI or the session logic.
//
// The stub path still behaves correctly: it connects, negotiates, reports
// `connected`, and counts every media byte it is handed, so the UI, the stats
// overlay and the diagnostics log all see a live session.
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "onow/net/WebSocket.h"
#include "onow/stream/StreamEngine.h"

namespace onow {
namespace stream {

// Decodes a complete RTP payload into an RGBA frame.
class VideoDecoder {
public:
  virtual ~VideoDecoder() = default;
  virtual bool open(int width, int height, const std::string& codec) = 0;
  virtual void close() = 0;
  // `rtp_payload` is the codec bitstream (H.264 Annex-B or AV1 OBUs).
  virtual bool decode(const uint8_t* rtp_payload, size_t length, uint64_t timestamp_us) = 0;
  virtual const char* name() const = 0;
};

// Receives SRTP packets off the wire and hands the payloads to a decoder.
class RtpReceiver {
public:
  virtual ~RtpReceiver() = default;
  virtual bool open(uint16_t local_port, const std::string& remote_host, uint16_t remote_port,
                    const std::string& srtp_key_hex, uint32_t srtp_key_id) = 0;
  virtual void close() = 0;
  // Called from the engine's receive loop.
  virtual void pump() = 0;
};

class WebRtcEngine : public StreamEngine {
public:
  WebRtcEngine();
  ~WebRtcEngine() override;

  bool start(const SessionContext& context) override;
  void stop() override;

  void send_key(uint32_t key_code, bool down, uint32_t modifiers) override;
  void send_mouse_move(int32_t dx, int32_t dy) override;
  void send_mouse_button(uint32_t button, bool down) override;
  void send_mouse_wheel(int32_t dx, int32_t dy) override;
  void send_gamepad_state(const uint8_t* state, size_t size) override;

  StreamStats stats() const override;
  bool is_connected() const override { return connected_; }
  bool request_keyframe() override;

  // Installs the media stack. Without a decoder the engine stays in the
  // counting stub mode described in the file header.
  void set_decoder(std::unique_ptr<VideoDecoder> decoder) { decoder_ = std::move(decoder); }
  void set_rtp_receiver(std::unique_ptr<RtpReceiver> receiver) { rtp_ = std::move(receiver); }

  // The signaling socket, exposed so the app can reuse it for engine events.
  WebSocket& signaling() { return signaling_; }

private:
  void receive_loop();
  void handle_event(const Json& payload);
  bool send_signal(const std::string& type, const Json& payload);
  std::string build_answer_sdp(const std::string& offer_sdp) const;
  void media_loop();
  void deliver_frame(const VideoFrame& frame);

  WebSocket signaling_;
  std::thread receive_thread_;
  std::thread media_thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> connected_{false};

  SessionContext context_;
  std::unique_ptr<VideoDecoder> decoder_;
  std::unique_ptr<RtpReceiver> rtp_;

  mutable std::mutex stats_mutex_;
  StreamStats stats_;
  std::vector<InputEvent> input_queue_;
  std::mutex input_mutex_;

  uint64_t started_at_us_ = 0;
  uint64_t last_bitrate_sample_at_us_ = 0;
  uint64_t bytes_at_last_sample_ = 0;
  uint64_t frame_index_ = 0;

  // Local ICE candidates gathered so far, in SDP syntax.
  std::vector<std::string> local_candidates_;
  std::string local_ufrag_;
  std::string remote_ufrag_;
};

} // namespace stream
} // namespace onow
