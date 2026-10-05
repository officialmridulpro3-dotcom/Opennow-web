// Minimal JSON. Ordered objects (NVIDIA's payloads are stable, and keeping key
// order makes `curl`-style debugging painless), no allocation-heavy tree walks,
// and a compact-but-readable writer. Deliberately not a general purpose
// library — it exists so the service layer never touches raw strings.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace onow {

class Json {
public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Json() : type_(Type::Null) {}
  Json(std::nullptr_t) : type_(Type::Null) {}
  Json(bool value) : type_(Type::Bool), bool_(value) {}
  Json(int value) : type_(Type::Number), number_(value) {}
  Json(unsigned value) : type_(Type::Number), number_(value) {}
  Json(long value) : type_(Type::Number), number_(value) {}
  Json(unsigned long value) : type_(Type::Number), number_(static_cast<double>(value)) {}
  Json(long long value) : type_(Type::Number), number_(static_cast<double>(value)) {}
  Json(float value) : type_(Type::Number), number_(value) {}
  Json(double value) : type_(Type::Number), number_(value) {}
  Json(const char* value) : type_(Type::String), string_(value ? value : "") {}
  Json(std::string value) : type_(Type::String), string_(std::move(value)) {}

  static Json array();
  static Json object();

  Type type() const { return type_; }
  bool is_null() const { return type_ == Type::Null; }
  bool is_bool() const { return type_ == Type::Bool; }
  bool is_number() const { return type_ == Type::Number; }
  bool is_string() const { return type_ == Type::String; }
  bool is_array() const { return type_ == Type::Array; }
  bool is_object() const { return type_ == Type::Object; }

  bool as_bool(bool fallback = false) const;
  double as_number(double fallback = 0.0) const;
  int as_int(int fallback = 0) const;
  long long as_ll(long long fallback = 0) const;
  const std::string& as_string() const; // "" for non-strings
  std::string as_string(const std::string& fallback) const;

  // Object access.
  const Json* find(const std::string& key) const;
  Json& operator[](const std::string& key);
  bool has(const std::string& key) const;
  void erase(const std::string& key);
  std::vector<std::string> keys() const;

  // Array access.
  size_t size() const;
  const Json& at(size_t index) const;
  Json& at(size_t index);
  const Json& operator[](size_t index) const;
  Json& operator[](size_t index);
  void push_back(Json value);

  // Serialisation.
  std::string dump(bool pretty = false) const;

  // Parsing. Returns false and fills `error` on malformed input.
  static bool parse(const std::string& text, Json& out, std::string* error = nullptr);
  static Json parse_or(const std::string& text, Json fallback = Json());

  // Null singleton for `find()` misses.
  static const Json& null();

private:
  Type type_;
  bool bool_ = false;
  double number_ = 0.0;
  std::string string_;
  std::vector<Json> array_;
  std::vector<std::pair<std::string, Json>> object_;

  Json* find_mut(const std::string& key);
  void dump_impl(std::string& out, bool pretty, int depth) const;
};

} // namespace onow
