// See WebRtcEngine.h for the scope of what is and is not implemented here.
#include "onow/stream/WebRtcEngine.h"

#include "onow/Log.h"
#include "onow/Str.h"
#include "onow/Time.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace onow {
namespace stream {

namespace {

// A minimal offer parser: we only need the media sections' codec, direction
// and the a=setup / ice-ufrag lines to answer.
struct MediaSection {
  std::string type;      // "audio" | "video"
  int port = 0;
  std::vector<int> payload_types;
  std::vector<std::string> codecs; // "H264", "opus", …
  bool send_only = false;
  std::string mid;
};

std::vector<MediaSection> parse_offer(const std::string& sdp) {
  std::vector<MediaSection> sections;
  MediaSection current;
  bool in_section = false;
  std::string rtpmap_codec;

  for (const std::string& raw_line : str_split(sdp, '\n')) {
    std::string line = str_trim(raw_line);
    if (line.empty()) continue;
    if (str_starts_with(line, "m=")) {
      if (in_section) sections.push_back(current);
      current = MediaSection();
      in_section = true;
      const std::vector<std::string> parts = str_split(line, ' ');
      current.type = parts.size() > 0 ? str_split(parts[0], '=')[1] : "";
      current.port = parts.size() > 1 ? str_to_int(parts[1], 0) : 0;
      for (size_t i = 3; i < parts.size(); ++i) current.payload_types.push_back(str_to_int(parts[i], 0));
      continue;
    }
    if (!in_section) continue;
    if (str_starts_with(line, "a=mid:")) {
      current.mid = line.substr(6);
    } else if (str_starts_with(line, "a=rtpmap:")) {
      const std::string rest = line.substr(9);
      const size_t slash = rest.find('/');
      const std::string pt = rest.substr(0, rest.find(' '));
      rtpmap_codec = slash == std::string::npos ? rest : rest.substr(0, slash);
      str_upper(rtpmap_codec);
      // Keep only the codec family, not the profile ("H264/90000" → "H264").
      const size_t profile = rtpmap_codec.find_first_of("-.");
      if (profile != std::string::npos) rtpmap_codec = rtpmap_codec.substr(0, profile);
      current.codecs.push_back(rtpmap_codec);
      (void)pt;
    } else if (str_starts_with(line, "a=sendonly") || str_starts_with(line, "a=recvonly")) {
      current.send_only = str_starts_with(line, "a=sendonly");
    }
  }
  if (in_section) sections.push_back(current);
  return sections;
}

} // namespace

WebRtcEngine::WebRtcEngine() = default;

WebRtcEngine::~WebRtcEngine() { stop(); }

bool WebRtcEngine::start(const SessionContext& context) {
  if (running_) return false;
  context_ = context;
  started_at_us_ = now_us();
  last_bitrate_sample_at_us_ = started_at_us_;

  // The signaling URL comes from the session; fall back to the backend root.
  std::string url = context.signaling_url;
  if (url.empty()) {
    ONOW_ERROR("WebRTC", "session carried no signaling URL");
    return false;
  }
  url = str_lower(url);
  if (str_starts_with(url, "https://")) url = "wss://" + url.substr(8);
  else if (str_starts_with(url, "http://")) url = "ws://" + url.substr(7);
  else if (str_starts_with(url, "wss://") || str_starts_with(url, "ws://")) {
    // already fine
  } else {
    url = "wss://" + url;
  }
  if (url.find("/api/signaling") == std::string::npos) {
    if (url.back() != '/') url += "/";
    url += "api/signaling";
  }

  std::string error;
  if (!signaling_.connect(url, {}, error)) {
    ONOW_ERROR("WebRTC", "signaling connect failed: %s", error.c_str());
    emit_event(StreamEvent::Failed, error);
    return false;
  }

  signaling_.on_message([this](const std::string& text, bool binary) {
    if (binary) return;
    Json parsed = Json::parse_or(text);
    const Json* type = parsed.find("type");
    if (!type || type->as_string() != "event") return;
    const Json* payload = parsed.find("payload");
    if (payload) handle_event(*payload);
  });
  signaling_.on_state([this](WebSocket::State state, const std::string& reason) {
    if (state == WebSocket::State::Open) {
      Json payload = Json::object();
      payload["sessionId"] = Json(context_.session_id);
      payload["signalingServer"] = Json(context_.rtsps_endpoint);
      payload["signalingUrl"] = Json(context_.signaling_url);
      send_signal("connect", payload);
    } else if (state == WebSocket::State::Closed || state == WebSocket::State::Failed) {
      connected_ = false;
      emit_event(StreamEvent::Disconnected, reason);
    }
  });

  running_ = true;
  receive_thread_ = std::thread([this] { receive_loop(); });
  media_thread_ = std::thread([this] { media_loop(); });
  return true;
}

void WebRtcEngine::stop() {
  if (!running_) return;
  running_ = false;
  connected_ = false;
  signaling_.close(1000, "client stop");
  if (receive_thread_.joinable()) receive_thread_.join();
  if (media_thread_.joinable()) media_thread_.join();
  if (decoder_) decoder_->close();
  if (rtp_) rtp_->close();
}

void WebRtcEngine::receive_loop() {
  // The WebSocket owns its own reader thread; this loop exists so the engine
  // can tick the input flush and notice a stalled bridge.
  while (running_) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (signaling_.state() == WebSocket::State::Failed) {
      emit_event(StreamEvent::Failed, "signaling bridge failed");
      return;
    }
    if (signaling_.last_activity_ms() == 0) continue;
    const int64_t idle = now_ms() - signaling_.last_activity_ms();
    if (idle > 30000) {
      emit_event(StreamEvent::Disconnected, "signaling bridge idle for 30s");
      return;
    }
  }
}

void WebRtcEngine::media_loop() {
  // Drains the RTP receiver when one is installed and feeds the decoder.
  while (running_) {
    if (rtp_) rtp_->pump();
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
  }
}

bool WebRtcEngine::send_signal(const std::string& type, const Json& payload) {
  Json frame = Json::object();
  frame["type"] = Json(type);
  frame["payload"] = payload;
  return signaling_.send_text_async(frame.dump());
}

void WebRtcEngine::handle_event(const Json& payload) {
  const std::string type = payload.find("type")->as_string();

  if (type == "connected") {
    connected_ = true;
    emit_event(StreamEvent::Connected);
    return;
  }
  if (type == "offer") {
    const std::string sdp = payload.find("sdp")->as_string();
    const std::vector<MediaSection> sections = parse_offer(sdp);
    for (const MediaSection& section : sections) {
      if (section.type != "video") continue;
      if (!section.codecs.empty()) context_.codec = section.codecs.front();
    }
    // Answer, then start gathering candidates for the m-line we answered.
    const std::string answer = build_answer_sdp(sdp);
    Json out = Json::object();
    out["sdp"] = Json(answer);
    send_signal("answer", out);
    ONOW_INFO("WebRTC", "answered offer (%d media sections)", static_cast<int>(sections.size()));
    return;
  }
  if (type == "remote-ice") {
    if (const Json* candidate = payload.find("candidate")) {
      const Json* ufrag = candidate->find("usernameFragment");
      if (ufrag) remote_ufrag_ = ufrag->as_string();
      if (rtp_) {
        // A real stack would feed this to its ICE agent. Recorded for logs.
        ONOW_DEBUG("WebRTC", "remote candidate %s", candidate->find("candidate")->as_string().c_str());
      }
    }
    return;
  }
  if (type == "disconnected") {
    connected_ = false;
    emit_event(StreamEvent::Disconnected, payload.find("reason")->as_string());
    return;
  }
  if (type == "error") {
    emit_event(StreamEvent::Failed, payload.find("message")->as_string());
    return;
  }
  if (type == "log") {
    ONOW_DEBUG("WebRTC", "engine log: %s", payload.find("message")->as_string().c_str());
    return;
  }
}

std::string WebRtcEngine::build_answer_sdp(const std::string& offer_sdp) const {
  // A skeleton answer: same origin line, one video m-line echoing the offered
  // codec, recvonly, with our own ufrag and a dummy candidate. A complete
  // implementation would run ICE and DTLS here; see the header.
  const std::vector<MediaSection> sections = parse_offer(offer_sdp);
  std::string codec = context_.codec.empty() ? "H264" : context_.codec;

  std::string sdp;
  sdp += "v=0\r\n";
  sdp += "o=- 0 0 IN IP4 0.0.0.0\r\n";
  sdp += "s=OpenNOW\r\n";
  sdp += "t=0 0\r\n";
  sdp += "a=group:BUNDLE 0\r\n";
  sdp += "a=msid-semantic: WMS\r\n";
  sdp += "m=video 9 RTP/AVP 96\r\n";
  sdp += "c=IN IP4 0.0.0.0\r\n";
  sdp += "a=rtcp:9 IN IP4 0.0.0.0\r\n";
  sdp += "a=recvonly\r\n";
  sdp += "a=rtpmap:96 " + codec + "/90000\r\n";
  sdp += "a=fmtp:96 profile-level-id=42e01f;packetization-mode=1\r\n";
  sdp += "a=ice-ufrag:" + (local_ufrag_.empty() ? std::string("onow0000") : local_ufrag_) + "\r\n";
  sdp += "a=ice-pwd:0f1e2d3c4b5a69788796a5b4c3d2e1f0\r\n";
  sdp += "a=fingerprint:sha-256 00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:"
         "00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00\r\n";
  sdp += "a=setup:active\r\n";
  sdp += "a=candidate:1 1 udp 2113937151 0.0.0.0 9 typ host\r\n";
  (void)sections;
  return sdp;
}

void WebRtcEngine::send_key(uint32_t key_code, bool down, uint32_t modifiers) {
  InputEvent event;
  event.kind = InputEvent::Kind::Key;
  event.timestamp_us = now_us();
  event.code = key_code;
  event.down = down;
  event.dx = static_cast<int32_t>(modifiers);
  std::lock_guard<std::mutex> lock(input_mutex_);
  input_queue_.push_back(std::move(event));
}

void WebRtcEngine::send_mouse_move(int32_t dx, int32_t dy) {
  InputEvent event;
  event.kind = InputEvent::Kind::MouseMove;
  event.timestamp_us = now_us();
  event.dx = dx;
  event.dy = dy;
  std::lock_guard<std::mutex> lock(input_mutex_);
  input_queue_.push_back(std::move(event));
}

void WebRtcEngine::send_mouse_button(uint32_t button, bool down) {
  InputEvent event;
  event.kind = InputEvent::Kind::MouseButton;
  event.timestamp_us = now_us();
  event.code = button;
  event.down = down;
  std::lock_guard<std::mutex> lock(input_mutex_);
  input_queue_.push_back(std::move(event));
}

void WebRtcEngine::send_mouse_wheel(int32_t dx, int32_t dy) {
  InputEvent event;
  event.kind = InputEvent::Kind::MouseWheel;
  event.timestamp_us = now_us();
  event.dx = dx;
  event.dy = dy;
  std::lock_guard<std::mutex> lock(input_mutex_);
  input_queue_.push_back(std::move(event));
}

void WebRtcEngine::send_gamepad_state(const uint8_t* state, size_t size) {
  InputEvent event;
  event.kind = InputEvent::Kind::Gamepad;
  event.timestamp_us = now_us();
  event.payload.assign(state, state + size);
  std::lock_guard<std::mutex> lock(input_mutex_);
  input_queue_.push_back(std::move(event));
}

StreamStats WebRtcEngine::stats() const {
  std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(stats_mutex_));
  StreamStats copy = stats_;
  const uint64_t now = now_us();
  const double elapsed_us = static_cast<double>(now - last_bitrate_sample_at_us_);
  if (elapsed_us > 0) {
    const uint64_t delta_bytes = stats_.bytes_received - bytes_at_last_sample_;
    copy.bitrate_mbps = (static_cast<double>(delta_bytes) * 8.0) / elapsed_us;
    const_cast<WebRtcEngine*>(this)->last_bitrate_sample_at_us_ = now;
    const_cast<WebRtcEngine*>(this)->bytes_at_last_sample_ = stats_.bytes_received;
  }
  copy.connected = connected_;
  return copy;
}

bool WebRtcEngine::request_keyframe() {
  Json payload = Json::object();
  payload["reason"] = Json("backlog");
  payload["backlogFrames"] = Json(0.0);
  payload["attempt"] = Json(1.0);
  return send_signal("keyframe", payload);
}

void WebRtcEngine::deliver_frame(const VideoFrame& frame) {
  std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(stats_mutex_));
  stats_.frames_received = frame.frame_index;
  stats_.bytes_received += static_cast<uint64_t>(frame.stride * frame.height);
  stats_.width = static_cast<uint32_t>(frame.width);
  stats_.height = static_cast<uint32_t>(frame.height);
  emit_frame(frame);
}

} // namespace stream
} // namespace onow
