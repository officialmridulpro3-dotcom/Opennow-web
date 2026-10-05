#include "onow/net/WebSocket.h"

#include "onow/Crypto.h"
#include "onow/Log.h"
#include "onow/Str.h"
#include "onow/Time.h"
#include "onow/net/TlsSocket.h"
#include "onow/net/Url.h"

#include <atomic>
#include <cstring>

namespace onow {

namespace {

// The client-to-server masking key, per RFC 6455 §5.3.
void mask_payload(uint8_t* data, size_t len, const uint8_t key[4]) {
  for (size_t i = 0; i < len; ++i) data[i] ^= key[i & 3];
}

} // namespace

WebSocket::WebSocket() = default;

WebSocket::~WebSocket() { close(); }

bool WebSocket::connect(const std::string& url,
                        const std::vector<std::pair<std::string, std::string>>& headers,
                        std::string& error) {
  close();
  url_ = url;
  headers_ = headers;

  Url parsed;
  if (!url_parse(url, parsed) || (parsed.scheme != "ws" && parsed.scheme != "wss")) {
    error = "WebSocket URL must be ws:// or wss://";
    state_.store(State::Failed);
    return false;
  }

  socket_ = std::unique_ptr<TlsSocket>(new TlsSocket());
  if (!socket_->connect(parsed.host, parsed.effective_port(), parsed.is_tls(), error)) {
    socket_.reset();
    state_.store(State::Failed);
    return false;
  }

  // RFC 6455 opening handshake.
  const std::string key = base64_encode(random_hex(16));
  std::string handshake;
  handshake += "GET " + parsed.target() + " HTTP/1.1\r\n";
  handshake += "Host: " + parsed.host_header() + "\r\n";
  handshake += "Upgrade: websocket\r\n";
  handshake += "Connection: Upgrade\r\n";
  handshake += "Sec-WebSocket-Key: " + key + "\r\n";
  handshake += "Sec-WebSocket-Version: 13\r\n";
  for (const auto& entry : headers) {
    handshake += entry.first + ": " + entry.second + "\r\n";
  }
  handshake += "\r\n";
  if (!socket_->send_all(reinterpret_cast<const uint8_t*>(handshake.data()), handshake.size())) {
    error = "failed to send WebSocket handshake";
    close();
    state_.store(State::Failed);
    return false;
  }

  // Read the response head, one byte at a time: it is small and this avoids
  // guessing a buffer size.
  std::string head;
  char c = 0;
  while (head.size() < 64 * 1024) {
    const int n = socket_->recv_some(reinterpret_cast<uint8_t*>(&c), 1);
    if (n <= 0) break;
    head.push_back(c);
    if (head.size() >= 4 && head.compare(head.size() - 4, 4, "\r\n\r\n") == 0) break;
  }
  const size_t status_pos = head.find(' ');
  const int status = status_pos == std::string::npos ? 0 : str_to_int(head.substr(status_pos + 1, 3));
  if (status != 101) {
    error = "WebSocket upgrade rejected (HTTP " + to_string(status) + ")";
    close();
    state_.store(State::Failed);
    return false;
  }

  state_.store(State::Open);
  last_activity_ms_.store(now_ms());
  stop_ = false;
  reader_ = std::thread([this] { reader_loop(); });
  return true;
}

void WebSocket::close(unsigned short code, const std::string& reason) {
  const bool was_open = state_.load() == State::Open;
  state_.store(State::Closing);
  TlsSocket* socket = socket_.get();
  if (socket && was_open) {
    std::vector<uint8_t> payload;
    payload.push_back(static_cast<uint8_t>((code >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>(code & 0xFF));
    payload.insert(payload.end(), reason.begin(), reason.end());
    write_frame(0x8, payload.data(), payload.size());
  }
  stop_ = true;
  if (reader_.joinable()) reader_.join();
  if (socket) {
    socket->close();
    delete socket;
  }
  socket_.reset();
  state_.store(State::Closed);
}

bool WebSocket::write_frame(uint8_t opcode, const uint8_t* payload, size_t len) {
  TlsSocket* socket = socket_.get();
  if (!socket || !socket->connected()) return false;

  std::vector<uint8_t> frame;
  frame.push_back(0x80 | opcode); // FIN + opcode
  if (len < 126) {
    frame.push_back(static_cast<uint8_t>(0x80 | len));
  } else if (len <= 0xFFFF) {
    frame.push_back(static_cast<uint8_t>(0x80 | 126));
    frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(len & 0xFF));
  } else {
    frame.push_back(static_cast<uint8_t>(0x80 | 127));
    for (int i = 7; i >= 0; --i) {
      frame.push_back(static_cast<uint8_t>((static_cast<uint64_t>(len) >> (i * 8)) & 0xFF));
    }
  }
  uint8_t key[4];
  random_bytes(key, sizeof(key));
  frame.insert(frame.end(), key, key + 4);
  const size_t payload_offset = frame.size();
  frame.insert(frame.end(), payload, payload + len);
  mask_payload(frame.data() + payload_offset, len, key);
  return socket->send_all(frame.data(), frame.size());
}

bool WebSocket::read_exact(uint8_t* buffer, size_t len) {
  TlsSocket* socket = socket_.get();
  if (!socket) return false;
  size_t got = 0;
  while (got < len && !stop_) {
    const int n = socket_->recv_some(buffer + got, len - got);
    if (n < 0) return false;
    if (n == 0) return false;
    got += static_cast<size_t>(n);
  }
  return got == len;
}

void WebSocket::reader_loop() {
  TlsSocket* socket = socket_.get();
  if (!socket) return;
  std::vector<uint8_t> frame_header(2);
  while (!stop_ && socket->connected()) {
    if (!read_exact(frame_header.data(), frame_header.size())) break;
    const uint8_t fin_opcode = frame_header[0];
    const uint8_t len_byte = frame_header[1];
    const bool fin = (fin_opcode & 0x80) != 0;
    const uint8_t opcode = fin_opcode & 0x0F;
    const bool masked = (len_byte & 0x80) != 0;
    uint64_t length = len_byte & 0x7F;
    if (length == 126) {
      uint8_t extended[2];
      if (!read_exact(extended, sizeof(extended))) break;
      length = (static_cast<uint64_t>(extended[0]) << 8) | extended[1];
    } else if (length == 127) {
      uint8_t extended[8];
      if (!read_exact(extended, sizeof(extended))) break;
      length = 0;
      for (uint8_t byte : extended) length = (length << 8) | byte;
    }
    uint8_t mask[4] = {0, 0, 0, 0};
    if (masked && !read_exact(mask, sizeof(mask))) break;
    if (length > 32u * 1024u * 1024u) break; // refuse absurd frames
    std::vector<uint8_t> payload(static_cast<size_t>(length));
    if (length > 0 && !read_exact(payload.data(), payload.size())) break;
    if (masked) mask_payload(payload.data(), payload.size(), mask);

    last_activity_ms_.store(now_ms());

    if (opcode == 0x8) { // close
      state_.store(State::Closing);
      break;
    }
    if (opcode == 0x9) { // ping -> pong
      write_frame(0xA, payload.data(), payload.size());
      continue;
    }
    if (opcode == 0xA) continue; // pong

    // Continuation frames append to the pending buffer.
    pending_.insert(pending_.end(), payload.begin(), payload.end());
    if (!fin) continue;
    const bool binary = (opcode == 0x2) || (!pending_.empty() && opcode == 0x0 && false);
    const std::string message(reinterpret_cast<const char*>(pending_.data()), pending_.size());
    pending_.clear();
    if (on_message_) on_message_(message, binary || opcode == 0x2);
  }
  const bool was_open = state_.load() == State::Open;
  state_.store(State::Closed);
  if (was_open && on_state_) on_state_(State::Closed, "socket closed");
}

bool WebSocket::send_text(const std::string& text) {
  std::lock_guard<std::mutex> lock(send_mutex_);
  return write_frame(0x1, reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

bool WebSocket::send_binary(const std::vector<uint8_t>& data) {
  std::lock_guard<std::mutex> lock(send_mutex_);
  return write_frame(0x2, data.data(), data.size());
}

bool WebSocket::send_text_async(const std::string& text) {
  std::lock_guard<std::mutex> lock(send_mutex_);
  std::vector<uint8_t> frame;
  frame.push_back(0x81);
  const size_t len = text.size();
  if (len < 126) {
    frame.push_back(static_cast<uint8_t>(0x80 | len));
  } else if (len <= 0xFFFF) {
    frame.push_back(static_cast<uint8_t>(0x80 | 126));
    frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(len & 0xFF));
  } else {
    frame.push_back(static_cast<uint8_t>(0x80 | 127));
    for (int i = 7; i >= 0; --i) {
      frame.push_back(static_cast<uint8_t>((static_cast<uint64_t>(len) >> (i * 8)) & 0xFF));
    }
  }
  uint8_t key[4];
  random_bytes(key, sizeof(key));
  frame.insert(frame.end(), key, key + 4);
  const size_t offset = frame.size();
  frame.insert(frame.end(), text.begin(), text.end());
  mask_payload(frame.data() + offset, len, key);
  send_queue_.push_back(std::move(frame));
  return true;
}

bool WebSocket::send_binary_async(const std::vector<uint8_t>& data) {
  std::lock_guard<std::mutex> lock(send_mutex_);
  std::vector<uint8_t> frame;
  frame.push_back(0x82);
  const size_t len = data.size();
  if (len < 126) {
    frame.push_back(static_cast<uint8_t>(0x80 | len));
  } else {
    frame.push_back(static_cast<uint8_t>(0x80 | 126));
    frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(len & 0xFF));
  }
  uint8_t key[4];
  random_bytes(key, sizeof(key));
  frame.insert(frame.end(), key, key + 4);
  const size_t offset = frame.size();
  frame.insert(frame.end(), data.begin(), data.end());
  mask_payload(frame.data() + offset, len, key);
  send_queue_.push_back(std::move(frame));
  return true;
}

bool WebSocket::flush() {
  std::lock_guard<std::mutex> lock(send_mutex_);
  TlsSocket* socket = socket_.get();
  if (!socket) return false;
  for (const auto& frame : send_queue_) {
    if (!socket_->send_all(frame.data(), frame.size())) return false;
  }
  send_queue_.clear();
  return true;
}

} // namespace onow
