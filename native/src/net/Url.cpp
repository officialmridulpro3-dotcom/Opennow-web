#include "onow/net/Url.h"

#include "onow/Str.h"

namespace onow {

int Url::effective_port() const {
  if (port != 0) return port;
  if (scheme == "https" || scheme == "wss") return 443;
  return 80;
}

std::string Url::authority() const {
  if (port == 0) return host;
  if ((is_tls() && port == 443) || (!is_tls() && port == 80)) return host;
  return host + ":" + to_string(port);
}

std::string Url::target() const {
  std::string out = path.empty() ? "/" : path;
  if (!query.empty()) out += "?" + query;
  return out;
}

std::string Url::str() const { return scheme + "://" + authority() + target(); }

std::string Url::host_header() const {
  if (port == 0) return host;
  if ((is_tls() && port == 443) || (!is_tls() && port == 80)) return host;
  return host + ":" + to_string(port);
}

bool url_parse(const std::string& text, Url& out) {
  out = Url();
  size_t pos = 0;
  const size_t scheme_end = text.find("://");
  if (scheme_end == std::string::npos) return false;
  out.scheme = str_lower(text.substr(0, scheme_end));
  pos = scheme_end + 3;

  size_t authority_end = text.find_first_of("/?#", pos);
  if (authority_end == std::string::npos) authority_end = text.size();
  const std::string authority = text.substr(pos, authority_end - pos);

  const size_t at = authority.find('@');
  if (at != std::string::npos) {
    out.userinfo = authority.substr(0, at);
    out.host = authority.substr(at + 1);
  } else {
    out.host = authority;
  }

  // IPv6 literal: [::1]:8080
  if (!out.host.empty() && out.host.front() == '[') {
    const size_t close = out.host.find(']');
    if (close != std::string::npos) {
      const std::string rest = out.host.substr(close + 1);
      out.host = out.host.substr(0, close + 1);
      if (!rest.empty() && rest[0] == ':') out.port = str_to_int(rest.substr(1));
    }
  } else {
    const size_t colon = out.host.rfind(':');
    if (colon != std::string::npos) {
      out.port = str_to_int(out.host.substr(colon + 1));
      out.host = out.host.substr(0, colon);
    }
  }
  out.host = str_lower(out.host);

  pos = authority_end;
  if (pos < text.size() && text[pos] == '/') {
    const size_t query_start = text.find_first_of("?#", pos);
    out.path = text.substr(pos, (query_start == std::string::npos ? text.size() : query_start) - pos);
    pos = query_start == std::string::npos ? text.size() : query_start;
  } else {
    out.path = "/";
  }
  if (pos < text.size() && text[pos] == '?') {
    ++pos;
    const size_t frag = text.find('#', pos);
    out.query = text.substr(pos, (frag == std::string::npos ? text.size() : frag) - pos);
    pos = frag == std::string::npos ? text.size() : frag;
  }
  if (pos < text.size() && text[pos] == '#') {
    out.fragment = text.substr(pos + 1);
  }
  return out.valid();
}

std::string url_encode(const std::string& value) {
  static const char* kHex = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size() * 3);
  for (unsigned char c : value) {
    const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                            c == '~';
    if (unreserved) {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(kHex[c >> 4]);
      out.push_back(kHex[c & 0x0F]);
    }
  }
  return out;
}

std::string url_decode(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && i + 2 < value.size()) {
      const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      const int hi = hex(value[i + 1]);
      const int lo = hex(value[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    if (value[i] == '+') {
      out.push_back(' ');
      continue;
    }
    out.push_back(value[i]);
  }
  return out;
}

std::vector<QueryParam> query_parse(const std::string& query) {
  std::vector<QueryParam> out;
  for (const std::string& pair : str_split(query, '&')) {
    if (pair.empty()) continue;
    const size_t eq = pair.find('=');
    QueryParam param;
    if (eq == std::string::npos) {
      param.key = url_decode(pair);
    } else {
      param.key = url_decode(pair.substr(0, eq));
      param.value = url_decode(pair.substr(eq + 1));
    }
    out.push_back(param);
  }
  return out;
}

std::string query_build(const std::vector<QueryParam>& params) {
  std::string out;
  for (size_t i = 0; i < params.size(); ++i) {
    if (i) out += "&";
    out += url_encode(params[i].key) + "=" + url_encode(params[i].value);
  }
  return out;
}

std::string url_with_query(const std::string& base, const std::vector<QueryParam>& params) {
  if (params.empty()) return base;
  Url parsed;
  if (!url_parse(base, parsed)) return base;
  std::vector<QueryParam> merged = query_parse(parsed.query);
  for (const QueryParam& param : params) {
    bool replaced = false;
    for (QueryParam& existing : merged) {
      if (existing.key == param.key) {
        existing.value = param.value;
        replaced = true;
        break;
      }
    }
    if (!replaced) merged.push_back(param);
  }
  parsed.query = query_build(merged);
  return parsed.str();
}

} // namespace onow
