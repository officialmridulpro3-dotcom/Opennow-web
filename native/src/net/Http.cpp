#include "onow/net/Http.h"

#include "onow/Log.h"
#include "onow/Str.h"
#include "onow/net/TlsSocket.h"
#include "onow/net/Url.h"

#include <algorithm>
#include <cstring>

namespace onow {

namespace {

constexpr int kMaxHeaderBytes = 64 * 1024;

bool read_line(TlsSocket& socket, std::string& line) {
  line.clear();
  char c = 0;
  while (line.size() < kMaxHeaderBytes) {
    const int n = socket.recv_some(reinterpret_cast<uint8_t*>(&c), 1);
    if (n <= 0) return false;
    if (c == '\n') {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      return true;
    }
    line.push_back(c);
  }
  return false;
}

bool read_bytes(TlsSocket& socket, uint8_t* buffer, size_t len) {
  size_t got = 0;
  while (got < len) {
    const int n = socket.recv_some(buffer + got, len - got);
    if (n < 0) return false;
    if (n == 0) return got == len;
    got += static_cast<size_t>(n);
  }
  return true;
}

std::string lower_ascii(std::string s) {
  for (char& c : s) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool parse_status_line(const std::string& line, HttpResponse& response) {
  // HTTP/1.1 200 OK
  const size_t first = line.find(' ');
  if (first == std::string::npos) return false;
  const size_t second = line.find(' ', first + 1);
  if (second == std::string::npos) return false;
  response.status = str_to_int(line.substr(first + 1, second - first - 1));
  response.reason = line.substr(second + 1);
  return response.status > 0;
}

} // namespace

const std::string* HttpResponse::header(const std::string& name) const {
  const std::string wanted = lower_ascii(name);
  for (const auto& entry : headers) {
    if (lower_ascii(entry.first) == wanted) return &entry.second;
  }
  return nullptr;
}

std::string HttpResponse::header_or(const std::string& name, const std::string& fallback) const {
  const std::string* value = header(name);
  return value ? *value : fallback;
}

std::string HttpResponse::content_type() const { return header_or("Content-Type"); }

bool http_tls_available() { return TlsSocket::available(); }

const char* http_tls_backend() { return TlsSocket::backend_name(); }

namespace {

// Sends the request line + headers and reads the response head. `body_out` is
// left empty for HEAD and for 204/304 responses.
Result<HttpResponse> perform(const HttpRequest& request, const Url& url, bool is_head,
                             const std::function<bool(const char*, size_t)>* on_chunk) {
  TlsSocket socket;
  std::string error;
  if (!socket.connect(url.host, url.effective_port(), url.is_tls(), error)) {
    return Error::fail("connection failed: " + error);
  }

  std::string head;
  head += (is_head ? std::string("HEAD ") : request.method + " ") + url.target() + " HTTP/1.1\r\n";
  head += "Host: " + url.host_header() + "\r\n";
  bool has_accept = false;
  bool has_length = false;
  bool has_connection = false;
  for (const auto& entry : request.headers) {
    const std::string lower = lower_ascii(entry.first);
    if (lower == "accept") has_accept = true;
    if (lower == "content-length") has_length = true;
    if (lower == "connection") has_connection = true;
    head += entry.first + ": " + entry.second + "\r\n";
  }
  if (!has_accept) head += "Accept: */*\r\n";
  if (!request.body.empty() && !has_length) {
    head += "Content-Length: " + std::to_string(request.body.size()) + "\r\n";
  }
  if (!has_connection) head += "Connection: close\r\n";
  head += "\r\n";

  if (!socket.send_all(reinterpret_cast<const uint8_t*>(head.data()), head.size())) {
    return Error::fail("failed to send request headers");
  }
  if (!request.body.empty()) {
    if (!socket.send_all(reinterpret_cast<const uint8_t*>(request.body.data()),
                         request.body.size())) {
      return Error::fail("failed to send request body");
    }
  }

  HttpResponse response;
  std::string line;
  if (!read_line(socket, line) || !parse_status_line(line, response)) {
    return Error::fail("malformed HTTP response");
  }
  while (read_line(socket, line)) {
    if (line.empty()) break;
    const size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    const std::string name = str_trim(line.substr(0, colon));
    const std::string value = str_trim(line.substr(colon + 1));
    if (!name.empty()) response.headers.emplace_back(name, value);
  }

  const bool bodyless = is_head || response.status == 204 || response.status == 304;
  if (bodyless) return response;

  const std::string transfer = lower_ascii(response.header_or("Transfer-Encoding"));
  const std::string content_length = response.header_or("Content-Length");

  if (transfer.find("chunked") != std::string::npos) {
    // Read chunked bodies.
    while (read_line(socket, line)) {
      const size_t semicolon = line.find(';');
      const std::string size_text = str_trim(semicolon == std::string::npos ? line : line.substr(0, semicolon));
      const long long chunk = std::strtoll(size_text.c_str(), nullptr, 16);
      if (chunk <= 0) {
        read_line(socket, line); // trailing CRLF
        break;
      }
      std::vector<uint8_t> buffer(static_cast<size_t>(chunk));
      if (!read_bytes(socket, buffer.data(), buffer.size())) break;
      if (on_chunk) {
        if (!(*on_chunk)(reinterpret_cast<const char*>(buffer.data()), buffer.size())) break;
      } else {
        response.body.append(reinterpret_cast<const char*>(buffer.data()), buffer.size());
      }
      read_line(socket, line); // CRLF after each chunk
    }
    return response;
  }

  if (!content_length.empty()) {
    const long long length = std::strtoll(content_length.c_str(), nullptr, 10);
    if (length > 0) {
      std::vector<uint8_t> buffer(static_cast<size_t>(length));
      if (!read_bytes(socket, buffer.data(), buffer.size())) {
        return Error::fail("truncated response body");
      }
      if (on_chunk) {
        (*on_chunk)(reinterpret_cast<const char*>(buffer.data()), buffer.size());
      } else {
        response.body.assign(reinterpret_cast<const char*>(buffer.data()), buffer.size());
      }
    }
    return response;
  }

  // No length and no chunking: read until EOF (we asked for Connection: close).
  uint8_t buffer[16384];
  while (true) {
    const int n = socket.recv_some(buffer, sizeof(buffer));
    if (n < 0) break;
    if (n == 0) break;
    if (on_chunk) {
      if (!(*on_chunk)(reinterpret_cast<const char*>(buffer), static_cast<size_t>(n))) break;
    } else {
      response.body.append(reinterpret_cast<const char*>(buffer), static_cast<size_t>(n));
    }
  }
  return response;
}

} // namespace

Result<HttpResponse> http_request(const HttpRequest& request) {
  return http_request_streaming(request, nullptr);
}

Result<HttpResponse> http_request_streaming(
    const HttpRequest& request, const std::function<bool(const char*, size_t)>& on_chunk) {
  Url url;
  if (!url_parse(request.url, url)) {
    return Error::fail("invalid URL: " + request.url);
  }
  if (!url.is_tls() && !http_tls_available()) {
    // Plain http still works; https without TLS does not.
  }
  int redirects = 0;
  std::string current = request.url;
  HttpRequest current_request = request;
  while (true) {
    Url parsed;
    if (!url_parse(current, parsed)) return Error::fail("invalid URL: " + current);
    const bool is_head = str_upper(current_request.method) == "HEAD";
    Result<HttpResponse> response =
        perform(current_request, parsed, is_head, on_chunk ? &on_chunk : nullptr);
    if (!response.ok()) return response;
    const bool redirect = response->status >= 300 && response->status < 400;
    if (redirect && redirects < request.max_redirects) {
      const std::string location = response->header_or("Location");
      if (!location.empty()) {
        ++redirects;
        if (location.find("://") != std::string::npos) {
          current = location;
        } else if (!location.empty() && location[0] == '/') {
          current = parsed.scheme + "://" + parsed.authority() + location;
        } else {
          const size_t slash = parsed.path.find_last_of('/');
          const std::string dir = slash == std::string::npos ? "/" : parsed.path.substr(0, slash + 1);
          current = parsed.scheme + "://" + parsed.authority() + dir + location;
        }
        if (response->status == 303 || (response->status >= 301 && response->status <= 302 &&
                                        str_upper(current_request.method) != "HEAD")) {
          current_request.method = "GET";
          current_request.body.clear();
        }
        continue;
      }
    }
    return response;
  }
}

Result<Json> http_json(const HttpRequest& request) {
  Result<HttpResponse> response = http_request(request);
  if (!response.ok()) return response.error();
  if (!response->ok()) {
    return Error::fail("HTTP " + to_string(response->status) + " from " + request.url);
  }
  Json json;
  std::string error;
  if (!Json::parse(response->body, json, &error)) {
    return Error::fail("invalid JSON from " + request.url + ": " + error);
  }
  return json;
}

} // namespace onow
