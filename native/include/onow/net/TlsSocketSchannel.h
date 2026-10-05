// SChannel TLS backend for Windows, kept out of TlsSocket.cpp so that file
// stays readable. `TlsSocket` stores the `SchannelState` in its opaque
// `tls_ctx_` pointer and forwards to the three entry points below.
#pragma once

#include <string>
#include <vector>

namespace onow {

struct SchannelState;

// Creates the state object on first use. Returns false when initialisation
// (credentials handle) fails.
bool schannel_init(SchannelState*& state, std::string& error);
bool schannel_handshake(int socket, const std::string& host, SchannelState*& state,
                        std::string& error);
bool schannel_send(int socket, const uint8_t* data, size_t len, SchannelState* state);
int schannel_recv(int socket, uint8_t* buffer, size_t len, SchannelState* state);
void schannel_free(SchannelState*& state);

} // namespace onow
