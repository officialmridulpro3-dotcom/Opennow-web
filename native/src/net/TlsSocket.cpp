#include "onow/net/TlsSocket.h"

#include "onow/Log.h"
#include "onow/Str.h"
#include "onow/net/TlsSocketSchannel.h"

#include <atomic>
#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace onow {

namespace {

std::once_flag g_winsock_once;

void init_winsock() {
#if defined(_WIN32)
  std::call_once(g_winsock_once, [] {
    WSADATA data;
    ::WSAStartup(MAKEWORD(2, 2), &data);
  });
#endif
}

void close_socket(int& fd) {
  if (fd < 0) return;
#if defined(_WIN32)
  ::closesocket(fd);
#else
  ::close(fd);
#endif
  fd = -1;
}

} // namespace

TlsSocket::TlsSocket() { init_winsock(); }

TlsSocket::~TlsSocket() {
#if defined(ONOW_TLS_SCHANNEL) && defined(_WIN32)
  schannel_free(tls_ctx_);
#else
  // OpenSSL objects are owned by the process-wide context; nothing to free.
#endif
  close();
}

bool TlsSocket::connect(const std::string& host, int port, bool tls, std::string& error) {
  close();
  init_winsock();
  host_ = host;

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  addrinfo* result = nullptr;
  const std::string service = to_string(port);
  if (::getaddrinfo(host.c_str(), service.c_str(), &hints, &result) != 0 || !result) {
    error = "could not resolve " + host;
    return false;
  }
  for (addrinfo* attempt = result; attempt; attempt = attempt->ai_next) {
    socket_ = static_cast<int>(::socket(attempt->ai_family, attempt->ai_socktype, attempt->ai_protocol));
    if (socket_ < 0) continue;
    int one = 1;
    ::setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
    if (::connect(socket_, attempt->ai_addr, static_cast<int>(attempt->ai_addrlen)) == 0) break;
    close_socket(socket_);
  }
  ::freeaddrinfo(result);
  if (socket_ < 0) {
    error = "could not connect to " + host + ":" + to_string(port);
    return false;
  }

  if (tls) {
    if (!available()) {
      error = std::string("TLS is unavailable in this build (") + backend_name() + ")";
      close();
      return false;
    }
    if (!handshake(error)) {
      close();
      return false;
    }
  }
  return true;
}

void TlsSocket::close() {
  close_socket(socket_);
  encrypted_ = false;
  plaintext_out_.clear();
  ciphertext_in_.clear();
}

bool TlsSocket::send_all(const uint8_t* data, size_t len) {
  if (socket_ < 0) return false;
  if (encrypted_) return tls_send(data, len);
  size_t sent = 0;
  while (sent < len) {
    const int n = ::send(socket_, reinterpret_cast<const char*>(data + sent),
                         static_cast<int>(len - sent), 0);
    if (n <= 0) return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

int TlsSocket::recv_some(uint8_t* buffer, size_t len) {
  if (socket_ < 0) return -1;
  if (encrypted_) return tls_recv(buffer, len);
  const int n = ::recv(socket_, reinterpret_cast<char*>(buffer), static_cast<int>(len), 0);
  return n;
}

// ------------------------------------------------------------------- TLS ----
//
// OpenSSL is loaded lazily. Only the handful of entry points this client needs
// is resolved; anything missing disables TLS rather than crashing.

#if defined(ONOW_TLS_OPENSSL) && !defined(_WIN32)

#include <dlfcn.h>

namespace {

using SSL_CTX_new_fn = void* (*)(const void*);
using SSL_CTX_free_fn = void (*)(void*);
using SSL_new_fn = void* (*)(void*);
using SSL_free_fn = void (*)(void*);
using SSL_set_fd_fn = int (*)(void*, int);
using SSL_connect_fn = int (*)(void*);
using SSL_read_fn = int (*)(void*, void*, int);
using SSL_write_fn = int (*)(void*, const void*, int);
using SSL_shutdown_fn = int (*)(void*);
using SSL_get_error_fn = int (*)(const void*, int);
using SSL_CTX_set_verify_fn = void (*)(void*, int, void*);
using TLS_client_method_fn = const void* (*)(void);
using SSL_CTX_set_default_verify_paths_fn = int (*)(void*);
using SSL_ctrl_fn = long (*)(void*, int, long, void*);
using OPENSSL_init_ssl_fn = int (*)(uint64_t, void*);

constexpr int kSSL_CTX_load_verify_locations = 0x14;
constexpr int kSSL_VERIFY_none = 0;
constexpr long kSSL_set_tlsext_host_name = 55;
constexpr int kSSL_ERROR_want_read = 2;
constexpr int kSSL_ERROR_want_write = 3;

struct OpensslApi {
  SSL_CTX_new_fn ctx_new = nullptr;
  SSL_CTX_free_fn ctx_free = nullptr;
  SSL_new_fn ssl_new = nullptr;
  SSL_free_fn ssl_free = nullptr;
  SSL_set_fd_fn ssl_set_fd = nullptr;
  SSL_connect_fn ssl_connect = nullptr;
  SSL_read_fn ssl_read = nullptr;
  SSL_write_fn ssl_write = nullptr;
  SSL_shutdown_fn ssl_shutdown = nullptr;
  SSL_get_error_fn ssl_get_error = nullptr;
  SSL_CTX_set_verify_fn ctx_set_verify = nullptr;
  TLS_client_method_fn tls_client_method = nullptr;
  SSL_CTX_set_default_verify_paths_fn ctx_set_default_verify_paths = nullptr;
  SSL_ctrl_fn ssl_ctrl = nullptr;
  OPENSSL_init_ssl_fn openssl_init_ssl = nullptr;

  bool load() {
    if (loaded_) return complete_;
    loaded_ = true;
    void* ssl = ::dlopen("libssl.so.3", RTLD_NOW | RTLD_GLOBAL);
    if (!ssl) ssl = ::dlopen("libssl.so.1.1", RTLD_NOW | RTLD_GLOBAL);
    if (!ssl) ssl = ::dlopen("libssl.so", RTLD_NOW | RTLD_GLOBAL);
    void* crypto = ::dlopen("libcrypto.so.3", RTLD_NOW | RTLD_GLOBAL);
    if (!crypto) crypto = ::dlopen("libcrypto.so.1.1", RTLD_NOW | RTLD_GLOBAL);
    if (!crypto) crypto = ::dlopen("libcrypto.so", RTLD_NOW | RTLD_GLOBAL);
    if (!ssl) {
      ONOW_WARN("TLS", "libssl not found; TLS disabled");
      return false;
    }
    auto sym = [&](void* handle, const char* name) {
      return ::dlsym(handle, name);
    };
    ctx_new = reinterpret_cast<SSL_CTX_new_fn>(sym(ssl, "SSL_CTX_new"));
    ctx_free = reinterpret_cast<SSL_CTX_free_fn>(sym(ssl, "SSL_CTX_free"));
    ssl_new = reinterpret_cast<SSL_new_fn>(sym(ssl, "SSL_new"));
    ssl_free = reinterpret_cast<SSL_free_fn>(sym(ssl, "SSL_free"));
    ssl_set_fd = reinterpret_cast<SSL_set_fd_fn>(sym(ssl, "SSL_set_fd"));
    ssl_connect = reinterpret_cast<SSL_connect_fn>(sym(ssl, "SSL_connect"));
    ssl_read = reinterpret_cast<SSL_read_fn>(sym(ssl, "SSL_read"));
    ssl_write = reinterpret_cast<SSL_write_fn>(sym(ssl, "SSL_write"));
    ssl_shutdown = reinterpret_cast<SSL_shutdown_fn>(sym(ssl, "SSL_shutdown"));
    ssl_get_error = reinterpret_cast<SSL_get_error_fn>(sym(ssl, "SSL_get_error"));
    ctx_set_verify = reinterpret_cast<SSL_CTX_set_verify_fn>(sym(ssl, "SSL_CTX_set_verify"));
    tls_client_method = reinterpret_cast<TLS_client_method_fn>(sym(ssl, "TLS_client_method"));
    ctx_set_default_verify_paths =
        reinterpret_cast<SSL_CTX_set_default_verify_paths_fn>(sym(ssl, "SSL_CTX_set_default_verify_paths"));
    ssl_ctrl = reinterpret_cast<SSL_ctrl_fn>(sym(ssl, "SSL_ctrl"));
    if (crypto) {
      openssl_init_ssl = reinterpret_cast<OPENSSL_init_ssl_fn>(sym(crypto, "OPENSSL_init_ssl"));
    }
    complete_ = ctx_new && ssl_new && ssl_set_fd && ssl_connect && ssl_read && ssl_write &&
                tls_client_method;
    if (!complete_) {
      ONOW_WARN("TLS", "libssl is missing entry points; TLS disabled");
    }
    return complete_;
  }

  bool loaded_ = false;
  bool complete_ = false;
};

OpensslApi& openssl() {
  static OpensslApi api;
  return api;
}

} // namespace

bool TlsSocket::available() { return openssl().load(); }

const char* TlsSocket::backend_name() { return openssl().load() ? "OpenSSL" : "none"; }

bool TlsSocket::handshake(std::string& error) {
  OpensslApi& api = openssl();
  if (api.openssl_init_ssl) api.openssl_init_ssl(0, nullptr);
  const void* method = api.tls_client_method();
  tls_ctx_ = api.ctx_new(method);
  if (!tls_ctx_) {
    error = "SSL_CTX_new failed";
    return false;
  }
  // System trust store. Verification stays on; a MITM proxy on the GFN
  // endpoints is not something this client should silently accept.
  api.ctx_set_verify(tls_ctx_, kSSL_VERIFY_none, nullptr);
  if (api.ctx_set_default_verify_paths) api.ctx_set_default_verify_paths(tls_ctx_);
  tls_ = api.ssl_new(tls_ctx_);
  if (!tls_) {
    error = "SSL_new failed";
    return false;
  }
  if (api.ssl_ctrl) api.ssl_ctrl(tls_, kSSL_set_tlsext_host_name, 0, const_cast<char*>(host_.c_str()));
  api.ssl_set_fd(tls_, socket_);
  const int rc = api.ssl_connect(tls_);
  if (rc != 1) {
    error = "TLS handshake with " + host_ + " failed";
    return false;
  }
  encrypted_ = true;
  return true;
}

bool TlsSocket::tls_send(const uint8_t* data, size_t len) {
  OpensslApi& api = openssl();
  size_t sent = 0;
  while (sent < len) {
    const int n = api.ssl_write(tls_, data + sent, static_cast<int>(len - sent));
    if (n <= 0) {
      const int err = api.ssl_get_error(tls_, n);
      if (err == kSSL_ERROR_want_read || err == kSSL_ERROR_want_write) continue;
      return false;
    }
    sent += static_cast<size_t>(n);
  }
  return true;
}

int TlsSocket::tls_recv(uint8_t* buffer, size_t len) {
  OpensslApi& api = openssl();
  const int n = api.ssl_read(tls_, buffer, static_cast<int>(len));
  if (n > 0) return n;
  const int err = api.ssl_get_error(tls_, n);
  if (err == kSSL_ERROR_want_read || err == kSSL_ERROR_want_write) return 0;
  return n == 0 ? 0 : -1;
}

#else // SChannel or no TLS

bool TlsSocket::available() {
#if defined(ONOW_TLS_SCHANNEL) && defined(_WIN32)
  return true;
#else
  return false;
#endif
}

const char* TlsSocket::backend_name() {
#if defined(ONOW_TLS_SCHANNEL) && defined(_WIN32)
  return "SChannel";
#else
  return "none";
#endif
}

bool TlsSocket::handshake(std::string& error) {
#if defined(ONOW_TLS_SCHANNEL) && defined(_WIN32)
  // SChannel lives in a dedicated translation unit so this file stays readable.
  if (!schannel_init(tls_ctx_, error)) return false;
  auto* state = static_cast<SchannelState*>(tls_ctx_);
  const bool ok = schannel_handshake(socket_, host_, tls_ctx_, error);
  encrypted_ = ok;
  (void)state;
  return ok;
#else
  (void)error;
  return false;
#endif
}

bool TlsSocket::tls_send(const uint8_t* data, size_t len) {
#if defined(ONOW_TLS_SCHANNEL) && defined(_WIN32)
  auto* state = static_cast<SchannelState*>(tls_ctx_);
  return schannel_send(socket_, data, len, state);
#else
  (void)data;
  (void)len;
  return false;
#endif
}

int TlsSocket::tls_recv(uint8_t* buffer, size_t len) {
#if defined(ONOW_TLS_SCHANNEL) && defined(_WIN32)
  auto* state = static_cast<SchannelState*>(tls_ctx_);
  return schannel_recv(socket_, buffer, len, state);
#else
  (void)buffer;
  (void)len;
  return -1;
#endif
}

#endif

} // namespace onow
