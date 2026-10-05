#include "onow/Time.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace onow {

namespace {

// NTP-step-safe monotonic base. `now_ms()` uses the system clock so it still
// reports wall time, but deltas between two `now_ms()` calls can go negative
// when the clock jumps; monotonic_ms() never does.
std::atomic<int64_t> g_monotonic_base{0};
std::atomic<int64_t> g_wall_base{0};

struct ClockInit {
  ClockInit() {
    const auto steady = std::chrono::steady_clock::now().time_since_epoch();
    const auto wall = std::chrono::system_clock::now().time_since_epoch();
    g_monotonic_base.store(std::chrono::duration_cast<std::chrono::milliseconds>(steady).count());
    g_wall_base.store(std::chrono::duration_cast<std::chrono::milliseconds>(wall).count());
  }
};

const ClockInit kClockInit;

int64_t steady_now_ms() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

} // namespace

int64_t now_ms() { return steady_now_ms() - g_monotonic_base.load() + g_wall_base.load(); }

int64_t now_s() { return now_ms() / 1000; }

int64_t monotonic_ms() { return steady_now_ms(); }

std::string iso_utc(int64_t at_ms) {
  const time_t seconds = static_cast<time_t>(at_ms / 1000);
  const int millis = static_cast<int>(at_ms % 1000);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &seconds);
#else
  gmtime_r(&seconds, &tm);
#endif
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d", tm.tm_year + 1900,
                tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  std::string out(buf);
  if (millis > 0) {
    char frac[8];
    std::snprintf(frac, sizeof(frac), ".%03d", millis);
    out += frac;
  }
  out += "Z";
  return out;
}

int64_t now_us() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

int64_t parse_iso_utc(const std::string& text) {
  // Accepts "YYYY-MM-DDTHH:MM:SS[.mmm][Z|+HH:MM|-HH:MM]".
  std::tm tm{};
  int year = 0, mon = 1, day = 1, hour = 0, minute = 0, second = 0, millis = 0;
  int consumed = 0;
  if (std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%n", &year, &mon, &day, &hour,
                  &minute, &second, &consumed) < 6) {
    return 0;
  }
  if (consumed < static_cast<int>(text.size()) && text[static_cast<size_t>(consumed)] == '.') {
    int frac = 0;
    int frac_len = 0;
    if (std::sscanf(text.c_str() + consumed, ".%d%n", &frac, &frac_len) >= 1 && frac_len > 0) {
      millis = frac;
      // Normalise "5" -> 500ms, "05" -> 50ms.
      for (int i = frac_len - 1; i < 3; ++i) millis *= 10;
      consumed += frac_len;
    }
  }
  int offset_seconds = 0;
  if (consumed < static_cast<int>(text.size())) {
    const char sign = text[static_cast<size_t>(consumed)];
    if (sign == 'Z' || sign == 'z') {
      offset_seconds = 0;
    } else if (sign == '+' || sign == '-') {
      int oh = 0, om = 0;
      if (std::sscanf(text.c_str() + consumed + 1, "%2d:%2d", &oh, &om) >= 1) {
        offset_seconds = (oh * 3600 + om * 60) * (sign == '-' ? -1 : 1);
      }
    }
  }
  tm.tm_year = year - 1900;
  tm.tm_mon = mon - 1;
  tm.tm_mday = day;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_sec = second;
#if defined(_WIN32)
  const time_t utc = _mkgmtime(&tm);
#else
  const time_t utc = timegm(&tm);
#endif
  return static_cast<int64_t>(utc) * 1000 + millis - static_cast<int64_t>(offset_seconds) * 1000;
}

void sleep_ms(int64_t ms) {
  if (ms <= 0) return;
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

} // namespace onow
