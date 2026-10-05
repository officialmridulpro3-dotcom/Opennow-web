// Minimal RFC 6455 WebSocket client with TLS support.
//
// The web client talked to `/api/signaling` through the browser's WebSocket
// object. This is the same thing from scratch: a handshake over HTTP/1.1 with
// the `Sec-WebSocket-Key` dance, then framed text/binary messages with the
// client-side masking the RFC requires.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "onow/Result.h"
#include "onow/net/TlsSocket.h"

namespace onow {

class WebSocket {
public:
  enum class State { Closed, Connecting, Open, Closing, Failed };

  using MessageFn = std::function<void(const std::string&, bool binary)>;
  using StateFn = std::function<void(State, const std::string& reason)>;

  WebSocket();
  ~WebSocket();

  WebSocket(const WebSocket&) = delete;
  WebSocket& operator=(const WebSocket&) = delete;

  // Starts the reader thread. `url` must be ws:// or wss://.
  bool connect(const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers,
               std::string& error);
  void close(unsigned short code = 1000, const std::string& reason = "");

  bool send_text(const std::string& text);
  bool send_binary(const std::vector<uint8_t>& data);
  // Fire-and-forget: queues the message and returns immediately. The queue is
  // drained by `flush()`, which the app calls once per frame so a burst of
  // input packets becomes one syscall batch instead of N.
  bool send_text_async(const std::string& text);
  bool send_binary_async(const std::vector<uint8_t>& data);
  bool flush();

  State state() const { return state_.load(); }

  void on_message(MessageFn fn) { on_message_ = std::move(fn); }
  void on_state(StateFn fn) { on_state_ = std::move(fn); }

  // Timestamp of the last inbound frame, epoch millis (0 = never).
  int64_t last_activity_ms() const { return last_activity_ms_.load(); }

private:
  void reader_loop();
  bool write_frame(uint8_t opcode, const uint8_t* payload, size_t len);
  bool read_exact(uint8_t* buffer, size_t len);

  std::string url_;
  std::vector<std::pair<std::string, std::string>> headers_;
  MessageFn on_message_;
  StateFn on_state_;

  std::unique_ptr<TlsSocket> socket_;
  std::atomic<State> state_{State::Closed};
  std::atomic<int64_t> last_activity_ms_{0};
  std::thread reader_;
  bool stop_ = false;

  std::mutex send_mutex_;
  std::vector<std::vector<uint8_t>> send_queue_;
  std::vector<uint8_t> read_buffer_;
  std::vector<uint8_t> pending_; // partial frames
};

} // namespace onow
