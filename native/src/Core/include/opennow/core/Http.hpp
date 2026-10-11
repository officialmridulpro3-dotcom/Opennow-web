// OpenNOW native client — HTTP client abstraction.
//
// The interface exists so the GFN services can be unit tested with a scripted
// transport, and so the Windows implementation (WinHTTP, no external deps) can
// stay in one file. WinHTTP gives us HTTP/2, system proxy resolution and the
// OS certificate store without shipping OpenSSL or curl.

#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace opennow::core {

struct HttpHeader {
    std::string name;
    std::string value;
};

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::vector<HttpHeader> headers;
    std::string body;
    std::chrono::milliseconds timeout{30'000};
    // When false the request still goes through the system store but expired or
    // self-signed certificates are tolerated. Only the CloudMatch direct-IP poll
    // needs this: NVIDIA seats use short-lived certificates pinned to the IP.
    bool strictTls = true;
    // WinHTTP resolve/receive timeouts are separate; this bounds the whole call.
    bool followRedirects = true;
};

struct HttpResponse {
    int status = 0;
    std::vector<HttpHeader> headers;
    std::string body;
    std::string transportError;   // set when the request never got a response

    bool ok() const { return transportError.empty() && status >= 200 && status < 300; }
    std::optional<std::string> Header(std::string_view name) const;
};

class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    virtual HttpResponse Send(const HttpRequest& request) = 0;
};

using HttpClientPtr = std::shared_ptr<IHttpClient>;

// WinHTTP-backed implementation on Windows; a portable fallback elsewhere.
HttpClientPtr CreateHttpClient();

// ---- helpers -----------------------------------------------------------------

// application/x-www-form-urlencoded encoding (spaces as '+', per HTML forms).
std::string FormEncode(const std::vector<std::pair<std::string, std::string>>& fields);
std::string BuildQueryString(const std::vector<std::pair<std::string, std::string>>& fields);
std::string UrlEncode(std::string_view value);

}  // namespace opennow::core
