// Clocks. The web client leaned on `Date.now()` / `performance.now()`; the
// native client needs the same two clocks plus a few formatting helpers.
#pragma once

#include <cstdint>
#include <string>

namespace onow {

// Wall clock, epoch milliseconds.
int64_t now_ms();
// Wall clock, epoch seconds.
int64_t now_s();
// Wall clock, epoch microseconds.
int64_t now_us();
// Monotonic clock in milliseconds (safe for deltas, unaffected by NTP steps).
int64_t monotonic_ms();

// ISO-8601 UTC timestamp, e.g. "2026-10-05T13:36:53Z".
std::string iso_utc(int64_t at_ms);
// Parses the subset of ISO-8601 NVIDIA's APIs emit ("...Z" or with an offset).
int64_t parse_iso_utc(const std::string& text);

// Coarse sleep. Prefer the platform's event loop for anything long.
void sleep_ms(int64_t ms);

} // namespace onow
