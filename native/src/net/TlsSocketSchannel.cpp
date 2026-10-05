#include "onow/net/TlsSocketSchannel.h"

#if defined(_WIN32) && defined(ONOW_TLS_SCHANNEL)

#include <windows.h>
#include <security.h>
#include <schannel.h>
#include <winsock2.h>

#include <cstring>

#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")

namespace onow {

struct SchannelState {
  CredHandle cred{};
  CtxtHandle ctx{};
  bool has_ctx = false;
  SecPkgContext_StreamSizes sizes{};
  std::vector<uint8_t> read_buffer;   // decrypted application bytes
  std::vector<uint8_t> wire_in;       // raw bytes still to feed to InitializeSecurityContext
  std::vector<uint8_t> pending_out;   // raw bytes to write to the socket
  bool handshake_done = false;
};

bool schannel_init(SchannelState*& state, std::string& error) {
  if (state) return true;
  auto* created = new SchannelState();
  SCHANNEL_CRED cred{};
  cred.dwVersion = SCHANNEL_CRED_VERSION;
  cred.grbitEnabledProtocols = 0; // let the OS pick (TLS 1.2+ on Win10+)
  cred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_MANUAL_CRED_VALIDATION |
                 SCH_USE_STRONG_CRYPTO;
  const SECURITY_STATUS status = ::AcquireCredentialsHandleA(
      nullptr, const_cast<char*>(UNISP_NAME_A), SECPKG_CRED_OUTBOUND, nullptr, &cred, nullptr,
      nullptr, &created->cred, nullptr);
  if (status != SEC_E_OK) {
    error = "AcquireCredentialsHandle failed";
    delete created;
    return false;
  }
  state = created;
  return true;
}

static bool flush_pending(int socket, SchannelState* state) {
  size_t sent = 0;
  while (sent < state->pending_out.size()) {
    const int n = ::send(socket, reinterpret_cast<const char*>(state->pending_out.data() + sent),
                         static_cast<int>(state->pending_out.size() - sent), 0);
    if (n <= 0) return false;
    sent += static_cast<size_t>(n);
  }
  state->pending_out.clear();
  return true;
}

static bool handshake_step(int socket, SchannelState* state, const std::string& host,
                           std::string& error) {
  SecBuffer in_buffers[2]{};
  SecBufferDesc in_desc{SECBUFFER_VERSION, 2, in_buffers};
  if (!state->wire_in.empty()) {
    in_buffers[0].BufferType = SECBUFFER_TOKEN;
    in_buffers[0].pvBuffer = state->wire_in.data();
    in_buffers[0].cbBuffer = static_cast<unsigned long>(state->wire_in.size());
    in_buffers[1].BufferType = SECBUFFER_EMPTY;
  }

  SecBuffer out_buffers[2]{};
  SecBufferDesc out_desc{SECBUFFER_VERSION, 2, out_buffers};
  out_buffers[0].BufferType = SECBUFFER_TOKEN;
  out_buffers[1].BufferType = SECBUFFER_ALERT;

  unsigned long attributes = 0;
  SECURITY_STATUS status = ::InitializeSecurityContextA(
      &state->cred, state->has_ctx ? &state->ctx : nullptr,
      host.empty() ? nullptr : const_cast<char*>(host.c_str()),
      ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
          ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM,
      0, 0, state->wire_in.empty() ? nullptr : &in_desc, 0, &state->ctx, &out_desc, &attributes,
      nullptr);
  if (status == SEC_E_INCOMPLETE_MESSAGE) return true;  // need more bytes
  if (status == SEC_I_CONTINUE_NEEDED || status == SEC_E_OK) {
    state->has_ctx = true;
    if (out_buffers[0].pvBuffer && out_buffers[0].cbBuffer > 0) {
      state->pending_out.insert(state->pending_out.end(),
                                static_cast<const uint8_t*>(out_buffers[0].pvBuffer),
                                static_cast<const uint8_t*>(out_buffers[0].pvBuffer) +
                                    out_buffers[0].cbBuffer);
      ::FreeContextBuffer(out_buffers[0].pvBuffer);
    }
    if (!state->pending_out.empty() && !flush_pending(socket, state)) {
      error = "TLS handshake write failed";
      return false;
    }
    if (status == SEC_E_OK) {
      if (::QueryContextAttributesA(&state->ctx, SECPKG_ATTR_STREAM_SIZES, &state->sizes) !=
          SEC_E_OK) {
        error = "QueryContextAttributes(stream sizes) failed";
        return false;
      }
      state->handshake_done = true;
      state->wire_in.clear();
      return true;
    }
  } else {
    error = "InitializeSecurityContext failed";
    return false;
  }
  return true;
}

bool schannel_handshake(int socket, const std::string& host, SchannelState*& state,
                        std::string& error) {
  if (!schannel_init(state, error)) return false;

  uint8_t chunk[16384];
  int iterations = 0;
  while (!state->handshake_done) {
    if (++iterations > 64) {
      error = "TLS handshake stalled";
      return false;
    }
    if (!handshake_step(socket, state, host, error)) return false;
    if (state->handshake_done) break;
    const int n = ::recv(socket, reinterpret_cast<char*>(chunk), sizeof(chunk), 0);
    if (n <= 0) {
      error = "TLS handshake read failed";
      return false;
    }
    state->wire_in.insert(state->wire_in.end(), chunk, chunk + n);
  }
  return true;
}

bool schannel_send(int socket, const uint8_t* data, size_t len, SchannelState* state) {
  if (!state || !state->handshake_done) return false;
  size_t offset = 0;
  while (offset < len) {
    const size_t chunk = (len - offset) < state->sizes.cbMaximumMessage
                             ? (len - offset)
                             : static_cast<size_t>(state->sizes.cbMaximumMessage);
    std::vector<uint8_t> buffer(state->sizes.cbHeader + chunk + state->sizes.cbTrailer);
    std::memcpy(buffer.data() + state->sizes.cbHeader, data + offset, chunk);

    SecBuffer buffers[4]{};
    buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
    buffers[0].pvBuffer = buffer.data();
    buffers[0].cbBuffer = state->sizes.cbHeader;
    buffers[1].BufferType = SECBUFFER_DATA;
    buffers[1].pvBuffer = buffer.data() + state->sizes.cbHeader;
    buffers[1].cbBuffer = static_cast<unsigned long>(chunk);
    buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
    buffers[2].pvBuffer = buffer.data() + state->sizes.cbHeader + chunk;
    buffers[2].cbBuffer = state->sizes.cbTrailer;
    buffers[3].BufferType = SECBUFFER_EMPTY;
    SecBufferDesc desc{SECBUFFER_VERSION, 4, buffers};

    if (::EncryptMessage(&state->ctx, 0, &desc, 0) != SEC_E_OK) return false;
    const size_t total = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
    size_t sent = 0;
    while (sent < total) {
      const int n = ::send(socket, reinterpret_cast<const char*>(buffer.data() + sent),
                           static_cast<int>(total - sent), 0);
      if (n <= 0) return false;
      sent += static_cast<size_t>(n);
    }
    offset += chunk;
  }
  return true;
}

int schannel_recv(int socket, uint8_t* buffer, size_t len, SchannelState* state) {
  if (!state || !state->handshake_done) return -1;
  if (state->read_buffer.empty()) {
    // Feed the wire until a full record decodes into application bytes.
    for (int guard = 0; guard < 64 && state->read_buffer.empty(); ++guard) {
      SecBuffer buffers[4]{};
      std::vector<uint8_t> wire = state->wire_in;
      buffers[0].BufferType = SECBUFFER_DATA;
      buffers[0].pvBuffer = wire.empty() ? nullptr : wire.data();
      buffers[0].cbBuffer = static_cast<unsigned long>(wire.size());
      buffers[1].BufferType = SECBUFFER_EMPTY;
      buffers[2].BufferType = SECBUFFER_EMPTY;
      buffers[3].BufferType = SECBUFFER_EMPTY;
      SecBufferDesc desc{SECBUFFER_VERSION, 4, buffers};

      const SECURITY_STATUS status =
          ::DecryptMessage(&state->ctx, &desc, 0, nullptr);
      if (status == SEC_E_INCOMPLETE_MESSAGE) {
        uint8_t chunk[16384];
        const int n = ::recv(socket, reinterpret_cast<char*>(chunk), sizeof(chunk), 0);
        if (n <= 0) return 0;
        state->wire_in.insert(state->wire_in.end(), chunk, chunk + n);
        continue;
      }
      if (status == SEC_I_RENEGOTIATE) {
        state->handshake_done = false;
        state->wire_in.clear();
        std::string error;
        if (!schannel_handshake(socket, "", state, error)) return -1;
        continue;
      }
      if (status != SEC_E_OK) return -1;
      for (int i = 0; i < 4; ++i) {
        if (buffers[i].BufferType == SECBUFFER_DATA && buffers[i].cbBuffer > 0) {
          state->read_buffer.insert(state->read_buffer.end(),
                                    static_cast<const uint8_t*>(buffers[i].pvBuffer),
                                    static_cast<const uint8_t*>(buffers[i].pvBuffer) +
                                        buffers[i].cbBuffer);
        }
      }
      // Extra bytes after the decrypted record start the next one.
      for (int i = 0; i < 4; ++i) {
        if (buffers[i].BufferType == SECBUFFER_EXTRA && buffers[i].cbBuffer > 0) {
          std::vector<uint8_t> extra(
              static_cast<const uint8_t*>(buffers[i].pvBuffer),
              static_cast<const uint8_t*>(buffers[i].pvBuffer) + buffers[i].cbBuffer);
          state->wire_in = extra;
          break;
        }
      }
      if (state->read_buffer.empty()) return 0;
    }
  }
  if (state->read_buffer.empty()) return 0;
  const size_t count = len < state->read_buffer.size() ? len : state->read_buffer.size();
  std::memcpy(buffer, state->read_buffer.data(), count);
  state->read_buffer.erase(state->read_buffer.begin(), state->read_buffer.begin() + count);
  return static_cast<int>(count);
}

void schannel_free(SchannelState*& state) {
  if (!state) return;
  if (state->has_ctx) ::DeleteSecurityContext(&state->ctx);
  ::FreeCredentialsHandle(&state->cred);
  delete state;
  state = nullptr;
}

} // namespace onow

#else

namespace onow {
bool schannel_init(SchannelState*&, std::string&) { return false; }
bool schannel_handshake(int, const std::string&, SchannelState*&, std::string& error) {
  error = "SChannel is only available on Windows";
  return false;
}
bool schannel_send(int, const uint8_t*, size_t, SchannelState*) { return false; }
int schannel_recv(int, uint8_t*, size_t, SchannelState*) { return -1; }
void schannel_free(SchannelState*&) {}
} // namespace onow

#endif
