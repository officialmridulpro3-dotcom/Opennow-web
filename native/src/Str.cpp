#include "onow/Str.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace onow {

namespace {

bool is_space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

} // namespace

std::string str_trim(const std::string& s) {
  size_t b = 0;
  size_t e = s.size();
  while (b < e && is_space(s[b])) ++b;
  while (e > b && is_space(s[e - 1])) --e;
  return s.substr(b, e - b);
}

std::string str_trim_left(const std::string& s) {
  size_t b = 0;
  while (b < s.size() && is_space(s[b])) ++b;
  return s.substr(b);
}

std::string str_trim_right(const std::string& s) {
  size_t e = s.size();
  while (e > 0 && is_space(s[e - 1])) --e;
  return s.substr(0, e);
}

std::string str_lower(const std::string& s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

std::string str_upper(const std::string& s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return out;
}

bool str_starts_with(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool str_ends_with(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool str_contains(const std::string& s, const std::string& needle) {
  return s.find(needle) != std::string::npos;
}

bool str_iequals(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

bool str_icontains(const std::string& haystack, const std::string& needle) {
  if (needle.empty()) return true;
  if (haystack.size() < needle.size()) return false;
  const std::string h = str_lower(haystack);
  const std::string n = str_lower(needle);
  return h.find(n) != std::string::npos;
}

std::vector<std::string> str_split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string current;
  for (char c : s) {
    if (c == sep) {
      out.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  out.push_back(current);
  return out;
}

std::vector<std::string> str_split_any(const std::string& s, const std::string& seps) {
  std::vector<std::string> out;
  std::string current;
  for (char c : s) {
    if (seps.find(c) != std::string::npos) {
      if (!current.empty()) out.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) out.push_back(current);
  return out;
}

std::string str_join(const std::vector<std::string>& parts, const std::string& sep) {
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) out += sep;
    out += parts[i];
  }
  return out;
}

std::string str_replace_all(std::string s, const std::string& from, const std::string& to) {
  if (from.empty()) return s;
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
  return s;
}

std::string str_search_key(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    unsigned char u = static_cast<unsigned char>(c);
    if (std::isalnum(u)) out.push_back(static_cast<char>(std::tolower(u)));
  }
  return out;
}

size_t utf8_length(const std::string& s) {
  size_t count = 0;
  for (unsigned char c : s) {
    if ((c & 0xC0) != 0x80) ++count;
  }
  return count;
}

size_t utf8_offset(const std::string& s, size_t index) {
  size_t count = 0;
  for (size_t i = 0; i < s.size();) {
    if (count == index) return i;
    unsigned char c = static_cast<unsigned char>(s[i]);
    size_t step = 1;
    if ((c & 0xE0) == 0xC0) step = 2;
    else if ((c & 0xF0) == 0xE0) step = 3;
    else if ((c & 0xF8) == 0xF0) step = 4;
    i += step;
    ++count;
  }
  return s.size();
}

std::string utf8_substr(const std::string& s, size_t begin, size_t end) {
  if (begin >= end) return std::string();
  size_t b = utf8_offset(s, begin);
  size_t e = utf8_offset(s, end);
  if (b > s.size()) b = s.size();
  if (e > s.size()) e = s.size();
  if (e < b) e = b;
  return s.substr(b, e - b);
}

std::string to_string(int value) {
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%d", value);
  return buf;
}

std::string to_string(long long value) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%lld", value);
  return buf;
}

std::string to_string(unsigned value) {
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%u", value);
  return buf;
}

std::string to_string(double value, int precision) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.*f", precision, value);
  return buf;
}

std::string to_string(bool value) { return value ? "true" : "false"; }

bool str_to_ll(const std::string& s, long long& out) {
  if (s.empty()) return false;
  char* end = nullptr;
  errno = 0;
  long long value = std::strtoll(s.c_str(), &end, 10);
  if (errno != 0 || end == s.c_str()) return false;
  while (end && *end && is_space(*end)) ++end;
  if (end && *end) return false;
  out = value;
  return true;
}

bool str_to_double(const std::string& s, double& out) {
  if (s.empty()) return false;
  char* end = nullptr;
  double value = std::strtod(s.c_str(), &end);
  if (end == s.c_str()) return false;
  while (end && *end && is_space(*end)) ++end;
  if (end && *end) return false;
  out = value;
  return true;
}

int str_to_int(const std::string& s, int fallback) {
  long long value = 0;
  if (!str_to_ll(s, value)) return fallback;
  return static_cast<int>(value);
}

bool is_numeric_id(const std::string& s) {
  std::string trimmed = str_trim(s);
  if (trimmed.empty()) return false;
  size_t i = 0;
  if (trimmed[0] == '-' || trimmed[0] == '+') i = 1;
  if (i >= trimmed.size()) return false;
  for (; i < trimmed.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(trimmed[i]))) return false;
  }
  return true;
}

std::string bytes_to_hex(const uint8_t* data, size_t len) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out.push_back(digits[data[i] >> 4]);
    out.push_back(digits[data[i] & 0x0F]);
  }
  return out;
}

std::string bytes_to_hex(const std::string& data) {
  return bytes_to_hex(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

bool parse_resolution(const std::string& text, Resolution& out) {
  const std::string::size_type x = text.find('x');
  if (x == std::string::npos) return false;
  if (!is_numeric_id(text.substr(0, x))) return false;
  if (!is_numeric_id(text.substr(x + 1))) return false;
  out.width = str_to_int(text.substr(0, x));
  out.height = str_to_int(text.substr(x + 1));
  return out.width > 0 && out.height > 0;
}

std::string resolution_label(const std::string& text) {
  Resolution r;
  if (!parse_resolution(text, r)) return text;
  return to_string(r.height) + "p";
}

std::string format_duration_hms(long long seconds) {
  if (seconds < 0) seconds = 0;
  long long h = seconds / 3600;
  long long m = (seconds % 3600) / 60;
  long long s = seconds % 60;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld", h, m, s);
  return buf;
}

std::string format_duration_compact(long long seconds) {
  if (seconds < 60) return to_string(seconds) + "s";
  if (seconds < 3600) return to_string(seconds / 60) + "m";
  long long h = seconds / 3600;
  long long m = (seconds % 3600) / 60;
  if (h < 24) {
    return m > 0 ? to_string(h) + "h " + to_string(m) + "m" : to_string(h) + "h";
  }
  long long d = h / 24;
  return to_string(d) + "d " + to_string(h % 24) + "h";
}

std::string format_mmss(long long seconds) {
  if (seconds < 0) seconds = 0;
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%02lld:%02lld", seconds / 60, seconds % 60);
  return buf;
}

std::string format_clock_ms(int64_t ms) {
  if (ms < 0) ms = 0;
  return format_mmss(ms / 1000);
}

std::string format_bitrate_kbps(double kbps) {
  char buf[32];
  if (kbps >= 1000.0) std::snprintf(buf, sizeof(buf), "%.1f Mbps", kbps / 1000.0);
  else std::snprintf(buf, sizeof(buf), "%.0f kbps", kbps);
  return buf;
}

std::string format_bytes(long long bytes) {
  static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double value = static_cast<double>(bytes);
  int unit = 0;
  while (value >= 1024.0 && unit < 4) {
    value /= 1024.0;
    ++unit;
  }
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.1f %s", value, units[unit]);
  return buf;
}

std::string format_relative_time(int64_t at_ms, int64_t now_ms) {
  if (at_ms <= 0) return "never";
  int64_t delta = now_ms - at_ms;
  if (delta < 0) delta = 0;
  long long seconds = delta / 1000;
  if (seconds < 60) return "just now";
  long long minutes = seconds / 60;
  if (minutes < 60) return to_string(minutes) + " min ago";
  long long hours = minutes / 60;
  if (hours < 24) return to_string(hours) + "h ago";
  long long days = hours / 24;
  if (days < 30) return to_string(days) + "d ago";
  long long months = days / 30;
  if (months < 12) return to_string(months) + "mo ago";
  return to_string(days / 365) + "y ago";
}

} // namespace onow
