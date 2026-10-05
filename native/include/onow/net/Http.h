// HTTP client. Blocking, synchronous, and dependency-free.
//
// TLS is provided by whichever backend the build selected: SChannel on
// Windows (native, always present) or OpenSSL loaded through dlopen elsewhere.
// A plain-TCP-only build compiles and runs — every request to an https:// URL
// then fails with a clear error rather than crashing.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "onow/Json.h"
#include "onow/Result.h"

namespace onow {

struct HttpRequest {
  std::string method = "GET";
  std::string url;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  int timeout_ms = 30000;
  // Follows up to `max_redirects` 3xx responses (0 = never).
  int max_redirects = 5;
};

struct HttpResponse {
  int status = 0;
  std::string reason;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;

  const std::string* header(const std::string& name) const;
  std::string header_or(const std::string& name, const std::string& fallback = "") const;
  std::string content_type() const;
  bool ok() const { return status >= 200 && status < 300; }
};

// One-shot request. `error.message` carries a human readable failure.
Result<HttpResponse> http_request(const HttpRequest& request);

// Streaming variant: `on_chunk` is invoked as body bytes arrive. Return false
// from the callback to abort. Used by the region-latency probes.
Result<HttpResponse> http_request_streaming(const HttpRequest& request,
                                            const std::function<bool(const char*, size_t)>& on_chunk);

// Convenience: JSON body in, JSON body out. Parses the response and reports a
// decode failure rather than an empty object.
Result<Json> http_json(const HttpRequest& request);

// Whether TLS is usable in this build/host.
bool http_tls_available();
const char* http_tls_backend();

} // namespace onow
