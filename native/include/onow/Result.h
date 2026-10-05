// A minimal Result<T, Error> used across the service layer so failures carry
// the same shape as the web client's thrown errors: a human title, a
// description, and an optional GFN error code the launch error UI reads.
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace onow {

struct Error {
  std::string message;    // machine-ish one-liner, safe for logs
  std::string title;      // localised headline for the error card
  std::string description;// localised body for the error card
  int gfn_error_code = 0; // 0 = not a GFN-mapped error
  std::string code_label; // e.g. "GFN 404"

  bool ok() const { return message.empty() && title.empty(); }
  static Error fail(std::string message) {
    Error e;
    e.message = std::move(message);
    return e;
  }
  static Error fail(std::string title, std::string description, int gfn_error_code = 0) {
    Error e;
    e.message = std::move(title);
    e.title = std::move(title);
    e.description = std::move(description);
    e.gfn_error_code = gfn_error_code;
    if (gfn_error_code != 0) e.code_label = "GFN " + std::to_string(gfn_error_code);
    return e;
  }
};

template <typename T>
class Result {
public:
  Result(T value) : value_(std::move(value)), error_(std::nullopt) {}
  Result(const Error& error) : value_(std::nullopt), error_(error) {}
  Result(Error&& error) : value_(std::nullopt), error_(std::move(error)) {}

  bool ok() const { return value_.has_value(); }
  explicit operator bool() const { return ok(); }

  const T& value() const { return *value_; }
  T& value() { return *value_; }
  const T& operator*() const { return *value_; }
  T& operator*() { return *value_; }
  const T* operator->() const { return &*value_; }
  T* operator->() { return &*value_; }

  const Error& error() const { return *error_; }
  bool has_error() const { return error_.has_value(); }
  const Error& err() const { return *error_; }

  // Unwraps or returns `fallback` (and logs nothing — the caller already saw
  // the error when it propagated).
  T value_or(T fallback) const { return ok() ? *value_ : std::move(fallback); }

private:
  std::optional<T> value_;
  std::optional<Error> error_;
};

template <>
class Result<void> {
public:
  Result() : error_(std::nullopt) {}
  Result(const Error& error) : error_(error) {}
  Result(Error&& error) : error_(std::move(error)) {}

  bool ok() const { return !error_.has_value(); }
  explicit operator bool() const { return ok(); }
  const Error& err() const { return *error_; }

private:
  std::optional<Error> error_;
};

using Status = Result<void>;

} // namespace onow
