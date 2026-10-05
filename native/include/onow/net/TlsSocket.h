// A blocking TCP socket, optionally wrapped in TLS.
//
// The web client inherited TLS from the browser. The native client has to own
// it, and it does so without linking a TLS library:
//
//   * Windows  — SChannel (`InitializeSecurityContext` / `DecryptMessage`),
//                which is part of the OS.
//   * Elsewhere — OpenSSL's `libssl`/`libcrypto`, resolved at runtime with
//                dlopen so the binary still starts on a host without them
//                (it just refuses https:// and says so).
//   * Neither  — plain TCP only, reported through `tls_error()`.
//
// Everything above this header (HTTP, WebSocket, the GFN client, the signaling
// bridge) is transport-agnostic.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace onow {

class TlsSocket {
public:
  TlsSocket();
  ~TlsSocket();

  TlsSocket(const TlsSocket&) = delete;
  TlsSocket& operator=(const TlsSocket&) = delete;

  // Connects to `host:port` (DNS resolved synchronously). `tls` selects
  // encryption; when true and TLS is unavailable the call fails with a
  // descriptive error.
  bool connect(const std::string& host, int port, bool tls, std::string& error);
  void close();

  bool send_all(const uint8_t* data, size_t len);
  // Returns bytes read (0 = orderly shutdown, -1 = error).
  int recv_some(uint8_t* buffer, size_t len);

  bool connected() const { return socket_ >= 0; }
  bool encrypted() const { return encrypted_; }
  const std::string& last_error() const { return error_; }

  // True when this build/host can actually do TLS.
  static bool available();
  static const char* backend_name();

private:
  bool handshake(std::string& error);
  bool tls_send(const uint8_t* data, size_t len);
  int tls_recv(uint8_t* buffer, size_t len);

  std::string host_;
  int socket_ = -1;
  bool encrypted_ = false;
  std::string error_;
  std::mutex mutex_;

  // TLS context, kept opaque so the header needs no OpenSSL/SChannel types.
  void* tls_ctx_ = nullptr;
  void* tls_ = nullptr;
  std::vector<uint8_t> plaintext_out_;
  std::vector<uint8_t> ciphertext_in_;
};

} // namespace onow
