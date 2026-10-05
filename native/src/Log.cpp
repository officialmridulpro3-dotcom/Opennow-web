#include "onow/Log.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace onow {
namespace detail {

namespace {

void append_ll(std::string& out, long long value) {
  char buf[32];
  int n = std::snprintf(buf, sizeof(buf), "%lld", value);
  if (n > 0) out.append(buf, static_cast<size_t>(n));
}

void append_double(std::string& out, double value) {
  char buf[64];
  int n = std::snprintf(buf, sizeof(buf), "%.3f", value);
  if (n > 0) out.append(buf, static_cast<size_t>(n));
}

// Walks the format string once and substitutes the first N conversions using
// the supplied writer. Good enough for log lines; not a printf clone.
template <typename Fn>
std::string vformat(const char* fmt, Fn&& writer) {
  std::string out;
  if (!fmt) return out;
  for (const char* p = fmt; *p; ++p) {
    if (*p != '%') {
      out.push_back(*p);
      continue;
    }
    ++p;
    if (!*p) break;
    if (*p == '%') {
      out.push_back('%');
      continue;
    }
    // Skip width/precision so "%.2f" and "%4d" behave sanely.
    while (*p && (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0' ||
                  (*p >= '1' && *p <= '9') || *p == '.')) {
      ++p;
    }
    if (!*p) break;
    switch (*p) {
      case 's':
        out += writer('s');
        break;
      case 'd':
      case 'i':
      case 'x':
      case 'X':
      case 'u':
        out += writer('d');
        break;
      case 'f':
      case 'g':
        out += writer('f');
        break;
      default:
        out.push_back('%');
        out.push_back(*p);
        break;
    }
  }
  return out;
}

} // namespace

std::string format_multi(const char* fmt, const std::vector<std::string>& args) {
  size_t index = 0;
  return vformat(fmt, [&](char) -> std::string {
    if (index >= args.size()) return std::string();
    return args[index++];
  });
}

} // namespace detail

Log& Log::instance() {
  static Log log;
  return log;
}

void Log::write(LogLevel level, std::string category, std::string message) {
  if (static_cast<int>(level) < static_cast<int>(min_level_.load())) return;

  int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    ring_.push_back(LogEntry{level, std::move(category), std::move(message), now});
    while (ring_.size() > ring_capacity_) ring_.pop_front();
  }

  static const char* kNames[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR"};
  const char* name = kNames[static_cast<int>(level)];
  if (echo_.load()) {
    std::fprintf(stderr, "[%s] %s: %s\n", name,
                 ring_.empty() ? "?" : ring_.back().category.c_str(),
                 ring_.empty() ? "" : ring_.back().message.c_str());
  }
  if (forwarder_ && !ring_.empty()) {
    forwarder_(std::string(name) + " " + ring_.back().message);
  }
}

std::vector<LogEntry> Log::tail(size_t limit) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<LogEntry> out;
  if (ring_.empty()) return out;
  size_t start = ring_.size() > limit ? ring_.size() - limit : 0;
  out.reserve(ring_.size() - start);
  for (size_t i = start; i < ring_.size(); ++i) out.push_back(ring_[i]);
  return out;
}

void Log::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  ring_.clear();
}

void log_write(LogLevel level, const char* category, const std::string& message) {
  Log::instance().write(level, category ? category : "app", message);
}

} // namespace onow
