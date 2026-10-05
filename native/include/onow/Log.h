// Logging. One ring buffer, one sink, many categories.
//
// The web client funnelled every diagnostic through `console.log` plus a
// batched `/api/client-log` POST. The native client keeps the same shape: a
// bounded in-memory ring (so the Diagnostics screen and the in-stream HUD can
// read it) plus a stderr sink, with an optional forward to the backend so a
// single log shows both sides of the handshake.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace onow {

enum class LogLevel : int {
  Trace = 0,
  Debug = 1,
  Info = 2,
  Warn = 3,
  Error = 4,
};

struct LogEntry {
  LogLevel level;
  std::string category;
  std::string message;
  int64_t at_ms; // wall clock, epoch millis
};

class Log {
public:
  static Log& instance();

  void set_min_level(LogLevel level) { min_level_.store(level); }
  LogLevel min_level() const { return min_level_.load(); }

  void set_echo_to_stderr(bool enabled) { echo_.store(enabled); }
  bool echo_to_stderr() const { return echo_.load(); }

  void write(LogLevel level, std::string category, std::string message);

  // Newest-last snapshot of the ring buffer, capped at `limit` entries.
  std::vector<LogEntry> tail(size_t limit = 500) const;

  void clear();

  // Forwarded verbatim to the backend's client-log endpoint (best effort).
  using ForwardFn = void (*)(const std::string& line);
  void set_forwarder(ForwardFn fn) { forwarder_ = fn; }

private:
  Log() = default;
  mutable std::mutex mutex_;
  std::deque<LogEntry> ring_;
  size_t ring_capacity_ = 2000;
  std::atomic<LogLevel> min_level_{LogLevel::Info};
  std::atomic<bool> echo_{true};
  ForwardFn forwarder_ = nullptr;
};

// Free-function helpers: ONOW_INFO("Auth", "signed in as %s", name.c_str());
void log_write(LogLevel level, const char* category, const std::string& message);

} // namespace onow

#define ONOW_LOG(level, category, ...) ::onow::log_write(level, category, ::onow::detail::format(__VA_ARGS__))
#define ONOW_TRACE(category, ...) ONOW_LOG(::onow::LogLevel::Trace, category, __VA_ARGS__)
#define ONOW_DEBUG(category, ...) ONOW_LOG(::onow::LogLevel::Debug, category, __VA_ARGS__)
#define ONOW_INFO(category, ...) ONOW_LOG(::onow::LogLevel::Info, category, __VA_ARGS__)
#define ONOW_WARN(category, ...) ONOW_LOG(::onow::LogLevel::Warn, category, __VA_ARGS__)
#define ONOW_ERROR(category, ...) ONOW_LOG(::onow::LogLevel::Error, category, __VA_ARGS__)

namespace onow {
namespace detail {

// Substitution engine shared by every `format` overload. Implemented in
// Log.cpp so the header stays free of <sstream>.
std::string format_multi(const char* fmt, const std::vector<std::string>& args);

inline std::string log_arg(const char* value) { return value ? std::string(value) : std::string(); }
inline std::string log_arg(const std::string& value) { return value; }
inline std::string log_arg(bool value) { return value ? "true" : "false"; }
inline std::string log_arg(char value) { return std::string(1, value); }

// Tiny printf replacement: std::string format(fmt, args...) with %s/%d/%f/%x.
// Any argument count works; numbers render with their default precision.
template <typename T>
std::string log_arg(const T& value) {
  return std::to_string(value);
}

// No-argument case (and the reason this overload exists before the template).
inline std::string format() { return std::string(); }
inline std::string format(const char* fmt) { return fmt ? std::string(fmt) : std::string(); }

template <typename... Args>
std::string format(const char* fmt, Args&&... args) {
  const std::vector<std::string> values = {log_arg(args)...};
  return format_multi(fmt, values);
}

} // namespace detail
} // namespace onow
