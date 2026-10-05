// String helpers. UTF-8 aware where it matters (UI text, game titles), and
// dependency-free everywhere else. Mirrors the small helpers the web client
// kept in `utils/timeFormat.ts`, `utils/playtimeFormat.ts`, etc.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace onow {

std::string str_trim(const std::string& s);
std::string str_trim_left(const std::string& s);
std::string str_trim_right(const std::string& s);
std::string str_lower(const std::string& s);
std::string str_upper(const std::string& s);

bool str_starts_with(const std::string& s, const std::string& prefix);
bool str_ends_with(const std::string& s, const std::string& suffix);
bool str_contains(const std::string& s, const std::string& needle);
bool str_iequals(const std::string& a, const std::string& b);
bool str_icontains(const std::string& haystack, const std::string& needle);

std::vector<std::string> str_split(const std::string& s, char sep);
std::vector<std::string> str_split_any(const std::string& s, const std::string& seps);
std::string str_join(const std::vector<std::string>& parts, const std::string& sep);
std::string str_replace_all(std::string s, const std::string& from, const std::string& to);

// Removes every character outside [a-zA-Z0-9] and lowercases the rest. Used for
// the catalog's search-text normalisation (`GameInfo.searchText`).
std::string str_search_key(const std::string& s);

// Number of UTF-8 code points (not bytes) — ImGui measures in code points.
size_t utf8_length(const std::string& s);
// Byte offset of code point `index`, clamped to the string length.
size_t utf8_offset(const std::string& s, size_t index);
// Extracts code points [begin, end) as a valid UTF-8 substring.
std::string utf8_substr(const std::string& s, size_t begin, size_t end);

std::string to_string(int value);
std::string to_string(long long value);
std::string to_string(unsigned value);
std::string to_string(double value, int precision = 2);
std::string to_string(bool value);

bool str_to_ll(const std::string& s, long long& out);
bool str_to_double(const std::string& s, double& out);
int str_to_int(const std::string& s, int fallback = 0);

// Case-insensitive numeric parse used by the catalog for `"12345"` app ids.
bool is_numeric_id(const std::string& s);

std::string bytes_to_hex(const uint8_t* data, size_t len);
std::string bytes_to_hex(const std::string& data);

// "1920x1080" -> {1920,1080}; false when unparsable.
struct Resolution {
  int width = 0;
  int height = 0;
};
bool parse_resolution(const std::string& text, Resolution& out);
std::string resolution_label(const std::string& text); // "1920x1080" -> "1080p"

// Durations, matching the web client's formatters.
std::string format_duration_hms(long long seconds);
std::string format_duration_compact(long long seconds); // "12h 34m"
std::string format_mmss(long long seconds);
std::string format_clock_ms(int64_t ms);

// Bitrate / byte counts.
std::string format_bitrate_kbps(double kbps);
std::string format_bytes(long long bytes);

std::string format_relative_time(int64_t at_ms, int64_t now_ms); // "3 days ago"

} // namespace onow
