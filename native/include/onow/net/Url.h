// URL parsing / encoding, plus the tiny amount of query-string plumbing the
// GFN endpoints need.
#pragma once

#include <string>
#include <vector>

namespace onow {

struct Url {
  std::string scheme;
  std::string host;
  int port = 0; // 0 = default for the scheme
  std::string path = "/";
  std::string query;
  std::string fragment;
  std::string userinfo;

  bool valid() const { return !host.empty(); }
  bool is_tls() const { return scheme == "https" || scheme == "wss"; }
  int effective_port() const;
  std::string authority() const;      // host[:port]
  std::string target() const;         // path[?query]
  std::string str() const;            // scheme://authority/target
  std::string host_header() const;    // host[:port] only when non-default
};

bool url_parse(const std::string& text, Url& out);

std::string url_encode(const std::string& value);
std::string url_decode(const std::string& value);

struct QueryParam {
  std::string key;
  std::string value;
};

std::vector<QueryParam> query_parse(const std::string& query);
std::string query_build(const std::vector<QueryParam>& params);

// Appends `params` to `base`, replacing existing keys.
std::string url_with_query(const std::string& base, const std::vector<QueryParam>& params);

} // namespace onow
