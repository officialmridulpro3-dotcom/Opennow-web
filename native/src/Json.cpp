#include "onow/Json.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace onow {

namespace {

const Json kNull;

void escape_into(std::string& out, const std::string& s) {
  out.push_back('"');
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  out.push_back('"');
}

struct Parser {
  const char* p;
  const char* end;
  std::string error;

  bool fail(const char* message) {
    if (error.empty()) error = message;
    return false;
  }

  void skip_ws() {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
  }

  bool parse_value(Json& out, int depth) {
    if (depth > 128) return fail("nesting too deep");
    skip_ws();
    if (p >= end) return fail("unexpected end of input");
    switch (*p) {
      case 'n':
        if (end - p >= 4 && std::strncmp(p, "null", 4) == 0) {
          p += 4;
          out = Json();
          return true;
        }
        return fail("invalid literal");
      case 't':
        if (end - p >= 4 && std::strncmp(p, "true", 4) == 0) {
          p += 4;
          out = Json(true);
          return true;
        }
        return fail("invalid literal");
      case 'f':
        if (end - p >= 5 && std::strncmp(p, "false", 5) == 0) {
          p += 5;
          out = Json(false);
          return true;
        }
        return fail("invalid literal");
      case '"': {
        std::string s;
        if (!parse_string(s)) return false;
        out = Json(std::move(s));
        return true;
      }
      case '[': {
        ++p;
        out = Json::array();
        skip_ws();
        if (p < end && *p == ']') {
          ++p;
          return true;
        }
        while (true) {
          Json item;
          if (!parse_value(item, depth + 1)) return false;
          out.push_back(std::move(item));
          skip_ws();
          if (p >= end) return fail("unterminated array");
          if (*p == ',') {
            ++p;
            continue;
          }
          if (*p == ']') {
            ++p;
            return true;
          }
          return fail("expected ',' or ']'");
        }
      }
      case '{': {
        ++p;
        out = Json::object();
        skip_ws();
        if (p < end && *p == '}') {
          ++p;
          return true;
        }
        while (true) {
          skip_ws();
          if (p >= end || *p != '"') return fail("expected object key");
          std::string key;
          if (!parse_string(key)) return false;
          skip_ws();
          if (p >= end || *p != ':') return fail("expected ':'");
          ++p;
          Json value;
          if (!parse_value(value, depth + 1)) return false;
          out[key] = std::move(value);
          skip_ws();
          if (p >= end) return fail("unterminated object");
          if (*p == ',') {
            ++p;
            continue;
          }
          if (*p == '}') {
            ++p;
            return true;
          }
          return fail("expected ',' or '}'");
        }
      }
      default:
        return parse_number(out);
    }
  }

  bool parse_number(Json& out) {
    const char* start = p;
    if (p < end && (*p == '-' || *p == '+')) ++p;
    bool digits = false;
    while (p < end && *p >= '0' && *p <= '9') {
      ++p;
      digits = true;
    }
    if (p < end && *p == '.') {
      ++p;
      while (p < end && *p >= '0' && *p <= '9') {
        ++p;
        digits = true;
      }
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
      ++p;
      if (p < end && (*p == '-' || *p == '+')) ++p;
      while (p < end && *p >= '0' && *p <= '9') ++p;
    }
    if (!digits) return fail("invalid number");
    std::string text(start, static_cast<size_t>(p - start));
    out = Json(std::strtod(text.c_str(), nullptr));
    return true;
  }

  static void append_utf8(std::string& out, unsigned int cp) {
    if (cp <= 0x7F) {
      out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  bool parse_hex4(unsigned int& out) {
    if (end - p < 4) return fail("truncated \\u escape");
    unsigned int value = 0;
    for (int i = 0; i < 4; ++i) {
      char c = *p++;
      value <<= 4;
      if (c >= '0' && c <= '9') value |= static_cast<unsigned int>(c - '0');
      else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned int>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned int>(c - 'A' + 10);
      else return fail("invalid \\u escape");
    }
    out = value;
    return true;
  }

  bool parse_string(std::string& out) {
    if (p >= end || *p != '"') return fail("expected string");
    ++p;
    out.clear();
    while (p < end) {
      unsigned char c = static_cast<unsigned char>(*p);
      if (c == '"') {
        ++p;
        return true;
      }
      if (c == '\\') {
        ++p;
        if (p >= end) return fail("truncated escape");
        char e = *p++;
        switch (e) {
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/': out.push_back('/'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          case 'u': {
            unsigned int cp = 0;
            if (!parse_hex4(cp)) return false;
            if (cp >= 0xD800 && cp <= 0xDBFF && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
              p += 2;
              unsigned int low = 0;
              if (!parse_hex4(low)) return false;
              if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
              } else {
                append_utf8(out, cp);
                cp = low;
              }
            }
            append_utf8(out, cp);
            break;
          }
          default:
            return fail("invalid escape");
        }
        continue;
      }
      out.push_back(static_cast<char>(c));
      ++p;
    }
    return fail("unterminated string");
  }
};

} // namespace

const Json& Json::null() { return kNull; }

Json Json::array() {
  Json j;
  j.type_ = Type::Array;
  return j;
}

Json Json::object() {
  Json j;
  j.type_ = Type::Object;
  return j;
}

bool Json::as_bool(bool fallback) const {
  if (type_ == Type::Bool) return bool_;
  if (type_ == Type::Number) return number_ != 0.0;
  if (type_ == Type::String) return string_ == "true" || string_ == "1";
  return fallback;
}

double Json::as_number(double fallback) const {
  if (type_ == Type::Number) return number_;
  if (type_ == Type::String) return std::strtod(string_.c_str(), nullptr);
  if (type_ == Type::Bool) return bool_ ? 1.0 : 0.0;
  return fallback;
}

int Json::as_int(int fallback) const {
  const double value = as_number(static_cast<double>(fallback));
  if (!std::isfinite(value)) return fallback;
  return static_cast<int>(value);
}

long long Json::as_ll(long long fallback) const {
  const double value = as_number(static_cast<double>(fallback));
  if (!std::isfinite(value)) return fallback;
  return static_cast<long long>(value);
}

const std::string& Json::as_string() const {
  static const std::string kEmpty;
  return type_ == Type::String ? string_ : kEmpty;
}

std::string Json::as_string(const std::string& fallback) const {
  if (type_ == Type::String) return string_;
  if (type_ == Type::Bool) return bool_ ? "true" : "false";
  if (type_ == Type::Number) {
    char buf[32];
    if (number_ == static_cast<double>(static_cast<long long>(number_))) {
      std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(number_));
    } else {
      std::snprintf(buf, sizeof(buf), "%g", number_);
    }
    return buf;
  }
  return fallback;
}

const Json* Json::find(const std::string& key) const {
  if (type_ != Type::Object) return nullptr;
  for (const auto& entry : object_) {
    if (entry.first == key) return &entry.second;
  }
  return nullptr;
}

Json* Json::find_mut(const std::string& key) {
  if (type_ != Type::Object) return nullptr;
  for (auto& entry : object_) {
    if (entry.first == key) return &entry.second;
  }
  return nullptr;
}

Json& Json::operator[](const std::string& key) {
  if (type_ != Type::Object) {
    type_ = Type::Object;
    array_.clear();
    string_.clear();
  }
  if (Json* existing = find_mut(key)) return *existing;
  object_.emplace_back(key, Json());
  return object_.back().second;
}

bool Json::has(const std::string& key) const { return find(key) != nullptr; }

void Json::erase(const std::string& key) {
  if (type_ != Type::Object) return;
  for (auto it = object_.begin(); it != object_.end(); ++it) {
    if (it->first == key) {
      object_.erase(it);
      return;
    }
  }
}

std::vector<std::string> Json::keys() const {
  std::vector<std::string> out;
  if (type_ != Type::Object) return out;
  out.reserve(object_.size());
  for (const auto& entry : object_) out.push_back(entry.first);
  return out;
}

size_t Json::size() const {
  if (type_ == Type::Array) return array_.size();
  if (type_ == Type::Object) return object_.size();
  return 0;
}

const Json& Json::at(size_t index) const {
  if (type_ == Type::Array && index < array_.size()) return array_[index];
  if (type_ == Type::Object && index < object_.size()) return object_[index].second;
  return kNull;
}

Json& Json::at(size_t index) {
  if (type_ == Type::Array && index < array_.size()) return array_[index];
  if (type_ == Type::Object && index < object_.size()) return object_[index].second;
  static Json kNullMutable;
  kNullMutable = Json();
  return kNullMutable;
}

const Json& Json::operator[](size_t index) const { return at(index); }

Json& Json::operator[](size_t index) { return at(index); }

void Json::push_back(Json value) {
  if (type_ != Type::Array) {
    type_ = Type::Array;
    object_.clear();
    string_.clear();
  }
  array_.push_back(std::move(value));
}

void Json::dump_impl(std::string& out, bool pretty, int depth) const {
  const char* nl = pretty ? "\n" : "";
  const char* pad = pretty ? "  " : "";
  auto indent = [&](int d) {
    if (!pretty) return;
    out.push_back('\n');
    for (int i = 0; i < d; ++i) out += pad;
  };

  switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += bool_ ? "true" : "false"; break;
    case Type::Number: {
      if (number_ == static_cast<double>(static_cast<long long>(number_)) &&
          std::fabs(number_) < 9.0e15) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(number_));
        out += buf;
      } else {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%g", number_);
        out += buf;
      }
      break;
    }
    case Type::String: escape_into(out, string_); break;
    case Type::Array: {
      if (array_.empty()) {
        out += "[]";
        break;
      }
      out.push_back('[');
      for (size_t i = 0; i < array_.size(); ++i) {
        if (i) out.push_back(',');
        indent(depth + 1);
        array_[i].dump_impl(out, pretty, depth + 1);
      }
      indent(depth);
      out.push_back(']');
      break;
    }
    case Type::Object: {
      if (object_.empty()) {
        out += "{}";
        break;
      }
      out.push_back('{');
      for (size_t i = 0; i < object_.size(); ++i) {
        if (i) out.push_back(',');
        indent(depth + 1);
        escape_into(out, object_[i].first);
        out += pretty ? ": " : ":";
        object_[i].second.dump_impl(out, pretty, depth + 1);
      }
      indent(depth);
      out.push_back('}');
      break;
    }
  }
  (void)nl;
}

std::string Json::dump(bool pretty) const {
  std::string out;
  dump_impl(out, pretty, 0);
  return out;
}

bool Json::parse(const std::string& text, Json& out, std::string* error) {
  Parser parser{text.data(), text.data() + text.size(), std::string()};
  if (!parser.parse_value(out, 0)) {
    if (error) *error = parser.error.empty() ? "malformed JSON" : parser.error;
    return false;
  }
  parser.skip_ws();
  if (parser.p != parser.end) {
    if (error) *error = "trailing data after JSON value";
    return false;
  }
  return true;
}

Json Json::parse_or(const std::string& text, Json fallback) {
  Json out;
  if (parse(text, out)) return out;
  return fallback;
}

} // namespace onow
