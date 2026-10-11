// OpenNOW native client — JSON parser/serialiser implementation.

#include "opennow/core/Json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace opennow::core::json {
namespace {

class Parser {
public:
    Parser(std::string_view text, std::size_t maxDepth) : text_(text), maxDepth_(maxDepth) {}

    ParseResult Run() {
        SkipWhitespace();
        Value root;
        if (!ParseValue(root, 0)) {
            return Fail(error_);
        }
        SkipWhitespace();
        if (pos_ != text_.size()) {
            return Fail("trailing content after JSON value");
        }
        ParseResult out;
        out.ok = true;
        out.value = std::move(root);
        return out;
    }

private:
    std::string_view text_;
    std::size_t maxDepth_;
    std::size_t pos_ = 0;
    std::string error_;

    ParseResult Fail(std::string message) const {
        ParseResult r;
        r.ok = false;
        r.message = std::move(message);
        r.offset = pos_;
        return r;
    }

    bool Eof() const { return pos_ >= text_.size(); }
    char Peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }
    char Next() { return pos_ < text_.size() ? text_[pos_++] : '\0'; }

    void SkipWhitespace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool Expect(char c) {
        if (Peek() != c) {
            error_ = std::string("expected '") + c + "'";
            return false;
        }
        ++pos_;
        return true;
    }

    bool Literal(std::string_view expected) {
        if (text_.size() - pos_ < expected.size() ||
            text_.compare(pos_, expected.size(), expected) != 0) {
            error_ = "invalid literal, expected " + std::string(expected);
            return false;
        }
        pos_ += expected.size();
        return true;
    }

    bool ParseValue(Value& out, std::size_t depth) {
        if (depth > maxDepth_) {
            error_ = "maximum nesting depth exceeded";
            return false;
        }
        if (Eof()) {
            error_ = "unexpected end of input";
            return false;
        }
        switch (Peek()) {
            case '{': return ParseObject(out, depth);
            case '[': return ParseArray(out, depth);
            case '"': {
                std::string s;
                if (!ParseString(s)) return false;
                out = Value(std::move(s));
                return true;
            }
            case 't': return Literal("true") ? (out = Value(true), true) : false;
            case 'f': return Literal("false") ? (out = Value(false), true) : false;
            case 'n': return Literal("null") ? (out = Value(), true) : false;
            default: return ParseNumber(out);
        }
    }

    bool ParseObject(Value& out, std::size_t depth) {
        if (!Expect('{')) return false;
        Object members;
        SkipWhitespace();
        if (Peek() == '}') {
            ++pos_;
            out = Value(std::move(members));
            return true;
        }
        for (;;) {
            SkipWhitespace();
            if (Peek() != '"') {
                error_ = "expected object key";
                return false;
            }
            std::string key;
            if (!ParseString(key)) return false;
            SkipWhitespace();
            if (!Expect(':')) return false;
            SkipWhitespace();
            Value child;
            if (!ParseValue(child, depth + 1)) return false;
            members.emplace_back(std::move(key), std::move(child));
            SkipWhitespace();
            const char c = Next();
            if (c == ',') continue;
            if (c == '}') break;
            error_ = "expected ',' or '}' in object";
            return false;
        }
        out = Value(std::move(members));
        return true;
    }

    bool ParseArray(Value& out, std::size_t depth) {
        if (!Expect('[')) return false;
        Array items;
        SkipWhitespace();
        if (Peek() == ']') {
            ++pos_;
            out = Value(std::move(items));
            return true;
        }
        for (;;) {
            SkipWhitespace();
            Value child;
            if (!ParseValue(child, depth + 1)) return false;
            items.push_back(std::move(child));
            SkipWhitespace();
            const char c = Next();
            if (c == ',') continue;
            if (c == ']') break;
            error_ = "expected ',' or ']' in array";
            return false;
        }
        out = Value(std::move(items));
        return true;
    }

    bool ParseHex4(std::uint32_t& out) {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            if (Eof()) {
                error_ = "truncated \\u escape";
                return false;
            }
            const char c = text_[pos_++];
            std::uint32_t digit = 0;
            if (c >= '0' && c <= '9') digit = static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') digit = static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = static_cast<std::uint32_t>(c - 'A' + 10);
            else {
                error_ = "invalid hex digit in \\u escape";
                return false;
            }
            v = (v << 4) | digit;
        }
        out = v;
        return true;
    }

    static void AppendUtf8(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
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

    bool ParseString(std::string& out) {
        if (!Expect('"')) return false;
        out.clear();
        for (;;) {
            if (Eof()) {
                error_ = "unterminated string";
                return false;
            }
            const char c = text_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (Eof()) {
                    error_ = "unterminated escape";
                    return false;
                }
                const char e = text_[pos_++];
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
                        std::uint32_t cp = 0;
                        if (!ParseHex4(cp)) return false;
                        if (cp >= 0xD800 && cp <= 0xDBFF && text_.size() - pos_ >= 6 &&
                            text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                            const std::size_t save = pos_;
                            pos_ += 2;
                            std::uint32_t low = 0;
                            if (!ParseHex4(low)) return false;
                            if (low >= 0xDC00 && low <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                            } else {
                                pos_ = save;
                            }
                        }
                        // Lone surrogates collapse to U+FFFD rather than failing:
                        // NVIDIA payloads occasionally carry unpaired escapes.
                        if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
                        AppendUtf8(out, cp);
                        break;
                    }
                    default:
                        error_ = "invalid escape character";
                        return false;
                }
                continue;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                error_ = "unescaped control character in string";
                return false;
            }
            out.push_back(c);
        }
    }

    bool ParseNumber(Value& out) {
        const std::size_t start = pos_;
        if (Peek() == '-') ++pos_;
        if (Eof()) {
            error_ = "truncated number";
            return false;
        }
        if (Peek() == '0') {
            ++pos_;
        } else if (Peek() >= '1' && Peek() <= '9') {
            while (!Eof() && Peek() >= '0' && Peek() <= '9') ++pos_;
        } else {
            error_ = "invalid number";
            return false;
        }

        bool isInteger = true;
        if (!Eof() && Peek() == '.') {
            isInteger = false;
            ++pos_;
            if (!(!Eof() && Peek() >= '0' && Peek() <= '9')) {
                error_ = "invalid fractional part";
                return false;
            }
            while (!Eof() && Peek() >= '0' && Peek() <= '9') ++pos_;
        }
        if (!Eof() && (Peek() == 'e' || Peek() == 'E')) {
            isInteger = false;
            ++pos_;
            if (!Eof() && (Peek() == '+' || Peek() == '-')) ++pos_;
            if (!(!Eof() && Peek() >= '0' && Peek() <= '9')) {
                error_ = "invalid exponent";
                return false;
            }
            while (!Eof() && Peek() >= '0' && Peek() <= '9') ++pos_;
        }

        const std::string slice(text_.substr(start, pos_ - start));
        if (isInteger) {
            out = Value(static_cast<std::int64_t>(std::strtoll(slice.c_str(), nullptr, 10)));
        } else {
            out = Value(std::strtod(slice.c_str(), nullptr));
        }
        return true;
    }
};

class DumpVisitor {
public:
    explicit DumpVisitor(bool pretty) : pretty_(pretty) {}

    void Write(const Value& v, int depth) {
        switch (v.type()) {
            case Type::Null: out_ += "null"; break;
            case Type::Bool: out_ += v.AsBool() ? "true" : "false"; break;
            case Type::Int: out_ += std::to_string(v.AsInt()); break;
            case Type::Double: WriteDouble(v.AsDouble()); break;
            case Type::String: WriteString(v.AsString()); break;
            case Type::Array: WriteArray(v, depth); break;
            case Type::Object: WriteObject(v, depth); break;
        }
    }

    std::string Take() { return std::move(out_); }

private:
    bool pretty_;
    std::string out_;

    void Indent(int depth) {
        if (!pretty_) return;
        out_.push_back('\n');
        out_.append(static_cast<std::size_t>(depth) * 2, ' ');
    }

    static void WriteDouble(std::string& out, double d) {
        if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 1e15) {
            // Integral doubles must not gain a ".0" or NVIDIA rejects sizes.
            out += std::to_string(static_cast<std::int64_t>(d));
            return;
        }
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.17g", d);
        out += buffer;
    }

    void WriteDouble(double d) { WriteDouble(out_, d); }

    void WriteString(std::string_view s) {
        out_.push_back('"');
        for (const char c : s) {
            switch (c) {
                case '"': out_ += "\\\""; break;
                case '\\': out_ += "\\\\"; break;
                case '\b': out_ += "\\b"; break;
                case '\f': out_ += "\\f"; break;
                case '\n': out_ += "\\n"; break;
                case '\r': out_ += "\\r"; break;
                case '\t': out_ += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buffer[8];
                        std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                                      static_cast<unsigned char>(c));
                        out_ += buffer;
                    } else {
                        out_.push_back(c);
                    }
            }
        }
        out_.push_back('"');
    }

    void WriteArray(const Value& v, int depth) {
        out_.push_back('[');
        const Array& items = v.Items();
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i != 0) out_.push_back(',');
            Indent(depth + 1);
            Write(items[i], depth + 1);
        }
        if (!items.empty()) Indent(depth);
        out_.push_back(']');
    }

    void WriteObject(const Value& v, int depth) {
        out_.push_back('{');
        const Object& members = v.Members();
        for (std::size_t i = 0; i < members.size(); ++i) {
            if (i != 0) out_.push_back(',');
            Indent(depth + 1);
            WriteString(members[i].first);
            out_.push_back(':');
            if (pretty_) out_.push_back(' ');
            Write(members[i].second, depth + 1);
        }
        if (!members.empty()) Indent(depth);
        out_.push_back('}');
    }
};

}  // namespace

ParseResult Value::Parse(std::string_view text, std::size_t maxDepth) {
    Parser parser(text, maxDepth);
    return parser.Run();
}

Value Value::ParseOrNull(std::string_view text) {
    ParseResult result = Parse(text);
    return result.ok ? std::move(result.value) : Value();
}

std::string Value::Dump(bool pretty) const {
    DumpVisitor visitor(pretty);
    visitor.Write(*this, 0);
    return visitor.Take();
}

const Value* Value::Find(std::string_view key) const noexcept {
    if (type_ != Type::Object) return nullptr;
    for (const auto& member : object_) {
        if (member.first == key) return &member.second;
    }
    return nullptr;
}

Value* Value::Find(std::string_view key) noexcept {
    if (type_ != Type::Object) return nullptr;
    for (auto& member : object_) {
        if (member.first == key) return &member.second;
    }
    return nullptr;
}

Value& Value::Set(std::string key, Value v) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        object_.clear();
    }
    if (Value* existing = Find(key)) {
        *existing = std::move(v);
        return *existing;
    }
    object_.emplace_back(std::move(key), std::move(v));
    return object_.back().second;
}

void Value::Erase(std::string_view key) {
    if (type_ != Type::Object) return;
    for (auto it = object_.begin(); it != object_.end(); ++it) {
        if (it->first == key) {
            object_.erase(it);
            return;
        }
    }
}

void Value::Push(Value v) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        array_.clear();
    }
    array_.push_back(std::move(v));
}

std::size_t Value::Size() const noexcept {
    switch (type_) {
        case Type::Array: return array_.size();
        case Type::Object: return object_.size();
        case Type::String: return string_.size();
        default: return 0;
    }
}

bool Value::AsBool(bool fallback) const noexcept {
    switch (type_) {
        case Type::Bool: return boolean_;
        case Type::Int: return int_ != 0;
        case Type::Double: return double_ != 0.0;
        case Type::String: return !string_.empty() && string_ != "false" && string_ != "0";
        default: return fallback;
    }
}

std::int64_t Value::AsInt(std::int64_t fallback) const noexcept {
    switch (type_) {
        case Type::Int: return int_;
        case Type::Double: return static_cast<std::int64_t>(double_);
        case Type::Bool: return boolean_ ? 1 : 0;
        case Type::String: {
            // CloudMatch echoes several numeric fields as strings.
            if (string_.empty()) return fallback;
            return std::strtoll(string_.c_str(), nullptr, 10);
        }
        default: return fallback;
    }
}

double Value::AsDouble(double fallback) const noexcept {
    switch (type_) {
        case Type::Double: return double_;
        case Type::Int: return static_cast<double>(int_);
        case Type::String: {
            if (string_.empty()) return fallback;
            return std::strtod(string_.c_str(), nullptr);
        }
        default: return fallback;
    }
}

std::string Value::AsString(std::string_view fallback) const {
    switch (type_) {
        case Type::String: return string_;
        case Type::Int: return std::to_string(int_);
        case Type::Bool: return boolean_ ? "true" : "false";
        case Type::Double: return std::to_string(double_);
        default: return std::string(fallback);
    }
}

std::optional<bool> Value::OptBool() const {
    if (!isBool()) return std::nullopt;
    return boolean_;
}

std::optional<std::int64_t> Value::OptInt() const {
    if (type_ != Type::Int) return std::nullopt;
    return int_;
}

std::optional<double> Value::OptDouble() const {
    if (type_ != Type::Double) return std::nullopt;
    return double_;
}

std::optional<std::string> Value::OptString() const {
    if (type_ != Type::String) return std::nullopt;
    return string_;
}

bool Value::operator==(const Value& other) const noexcept {
    if (type_ != other.type_) return false;
    switch (type_) {
        case Type::Null: return true;
        case Type::Bool: return boolean_ == other.boolean_;
        case Type::Int: return int_ == other.int_;
        case Type::Double: return double_ == other.double_;
        case Type::String: return string_ == other.string_;
        case Type::Array: return array_ == other.array_;
        case Type::Object: return object_ == other.object_;
    }
    return false;
}

}  // namespace opennow::core::json
