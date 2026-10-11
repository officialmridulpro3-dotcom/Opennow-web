// OpenNOW native client — outcome type.
//
// Every GFN call can fail in a way the UI must show (expired token, queue
// timeout, region down). We use an explicit outcome instead of exceptions
// because these failures are expected control flow, not bugs.

#pragma once

#include <optional>
#include <string>
#include <utility>

namespace opennow::core {

template <typename T>
struct Result {
    std::optional<T> value;
    std::string error;

    bool ok() const { return value.has_value(); }
    explicit operator bool() const { return value.has_value(); }
    const T& operator*() const { return *value; }
    T& operator*() { return *value; }
    const T* operator->() const { return &*value; }

    // Returns the value, or a caller-supplied fallback when the call failed.
    T ValueOr(T fallback) const { return value ? *value : std::move(fallback); }

    static Result Ok(T v) { return Result{std::move(v), {}}; }
    static Result Fail(std::string message) { return Result{std::nullopt, std::move(message)}; }
};

// Specialisation for calls that only report success or failure.
template <>
struct Result<void> {
    bool succeeded = false;
    std::string error;

    bool ok() const { return succeeded; }
    explicit operator bool() const { return succeeded; }

    static Result Ok() { return Result{true, {}}; }
    static Result Fail(std::string message) { return Result{false, std::move(message)}; }
};

}  // namespace opennow::core
