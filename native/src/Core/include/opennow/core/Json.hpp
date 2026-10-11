// OpenNOW native client — minimal dependency-free JSON (RFC 8259) DOM.
//
// The GFN protocol payloads are JSON end to end (auth, catalog, CloudMatch,
// signaling, settings, playtime). This module is deliberately small, strict and
// allocation-honest: it is the only parser the native client needs, so we avoid
// pulling a 900 KB single-header dependency into the build.
//
// Object members are kept in a vector of pairs so `Value` may stay incomplete
// while its own container members are declared (C++17 incomplete-type support
// covers vector/list/forward_list only — not map/unordered_map). Payload
// objects are small, so linear lookup beats the hash overhead in practice and
// preserves wire ordering, which makes dumps reproducible in tests.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace opennow::core::json {

class Value;

using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;

struct ParseResult;  // defined after Value, which it holds by value

enum class Type : std::uint8_t { Null, Bool, Int, Double, String, Array, Object };

class Value {
public:
    Value() noexcept = default;
    Value(std::nullptr_t) noexcept {}
    Value(bool v) noexcept : type_(Type::Bool), boolean_(v) {}
    Value(int v) noexcept : type_(Type::Int), int_(v) {}
    Value(unsigned v) noexcept : type_(Type::Int), int_(static_cast<std::int64_t>(v)) {}
    Value(std::int64_t v) noexcept : type_(Type::Int), int_(v) {}
    Value(std::size_t v) noexcept : type_(Type::Int), int_(static_cast<std::int64_t>(v)) {}
    Value(double v) noexcept : type_(Type::Double), double_(v) {}
    Value(std::string v) : type_(Type::String), string_(std::move(v)) {}
    Value(const char* v) : type_(Type::String), string_(v ? v : "") {}
    Value(std::string_view v) : type_(Type::String), string_(v) {}
    Value(Array v) : type_(Type::Array), array_(std::move(v)) {}
    Value(Object v) : type_(Type::Object), object_(std::move(v)) {}

    // ---- parsing -------------------------------------------------------
    static ParseResult Parse(std::string_view text, std::size_t maxDepth = 200);
    // Convenience: returns a Null value when the text is not valid JSON.
    static Value ParseOrNull(std::string_view text);

    // ---- serialisation -------------------------------------------------
    // Defaults to compact output. Pretty output uses two-space indentation.
    std::string Dump(bool pretty = false) const;

    // ---- type inspection ------------------------------------------------
    Type type() const noexcept { return type_; }
    bool isNull() const noexcept { return type_ == Type::Null; }
    bool isBool() const noexcept { return type_ == Type::Bool; }
    bool isNumber() const noexcept { return type_ == Type::Int || type_ == Type::Double; }
    bool isInt() const noexcept { return type_ == Type::Int; }
    bool isDouble() const noexcept { return type_ == Type::Double; }
    bool isString() const noexcept { return type_ == Type::String; }
    bool isArray() const noexcept { return type_ == Type::Array; }
    bool isObject() const noexcept { return type_ == Type::Object; }

    // ---- object access ---------------------------------------------------
    // Linear scan; returns nullptr when absent or when this is not an object.
    const Value* Find(std::string_view key) const noexcept;
    Value* Find(std::string_view key) noexcept;
    bool Contains(std::string_view key) const noexcept { return Find(key) != nullptr; }

    // Insert or overwrite a member (converts this value into an object).
    Value& Set(std::string key, Value v);
    void Erase(std::string_view key);

    // ---- array access ------------------------------------------------------
    void Push(Value v);
    std::size_t Size() const noexcept;
    bool Empty() const noexcept { return Size() == 0; }
    const Array& Items() const noexcept { return array_; }
    const Object& Members() const noexcept { return object_; }

    // ---- scalar coercion --------------------------------------------------
    // Every coercion accepts a fallback so call sites never throw on the
    // messy, partially-populated payloads NVIDIA actually returns.
    bool AsBool(bool fallback = false) const noexcept;
    std::int64_t AsInt(std::int64_t fallback = 0) const noexcept;
    double AsDouble(double fallback = 0.0) const noexcept;
    std::string AsString(std::string_view fallback = {}) const;
    std::optional<bool> OptBool() const;
    std::optional<std::int64_t> OptInt() const;
    std::optional<double> OptDouble() const;
    std::optional<std::string> OptString() const;

    bool operator==(const Value& other) const noexcept;
    bool operator!=(const Value& other) const noexcept { return !(*this == other); }

private:
    Type type_ = Type::Null;
    bool boolean_ = false;
    std::int64_t int_ = 0;
    double double_ = 0.0;
    std::string string_;
    Array array_;
    Object object_;
};

// Result of a parse attempt. Declared after `Value` so it can hold one by value.
struct ParseResult {
    bool ok = false;
    Value value;              // valid when ok == true
    std::string message;      // human readable failure reason
    std::size_t offset = 0;   // byte offset of the failure in the input
};

// Terse helpers for building payloads inline.
inline Value ObjectValue() { return Value{Object{}}; }
inline Value ArrayValue() { return Value{Array{}}; }

}  // namespace opennow::core::json
