#include "audio_studio/rpc/json_value.hpp"

#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace audio_studio::rpc {
namespace {

class Parser {
public:
  explicit Parser(const std::string& text) : text_(text) {}

  JsonValue parse() {
    JsonValue value = parseValue();
    skipSpace();
    if (pos_ != text_.size()) fail("unexpected trailing data");
    return value;
  }

private:
  JsonValue parseValue() {
    skipSpace();
    if (pos_ >= text_.size()) fail("unexpected end of JSON input");
    const char ch = text_[pos_];
    if (ch == 'n') return parseLiteral("null", JsonValue());
    if (ch == 't') return parseLiteral("true", JsonValue(true));
    if (ch == 'f') return parseLiteral("false", JsonValue(false));
    if (ch == '"') return JsonValue(parseString());
    if (ch == '[') return parseArray();
    if (ch == '{') return parseObject();
    if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) return parseNumber();
    fail("unexpected JSON token");
    return {};
  }

  JsonValue parseLiteral(const char* literal, JsonValue value) {
    const std::string expected(literal);
    if (text_.compare(pos_, expected.size(), expected) != 0) fail("invalid JSON literal");
    pos_ += expected.size();
    return value;
  }

  std::string parseString() {
    expect('"');
    std::string out;
    while (pos_ < text_.size()) {
      const char ch = text_[pos_++];
      if (ch == '"') return out;
      if (static_cast<unsigned char>(ch) < 0x20) fail("control character in JSON string");
      if (ch != '\\') {
        out.push_back(ch);
        continue;
      }
      if (pos_ >= text_.size()) fail("unterminated JSON escape");
      const char escaped = text_[pos_++];
      switch (escaped) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u':
          out += parseUnicodeEscape();
          break;
        default:
          fail("invalid JSON escape");
      }
    }
    fail("unterminated JSON string");
    return {};
  }

  std::string parseUnicodeEscape() {
    if (pos_ + 4 > text_.size()) fail("short JSON unicode escape");
    uint32_t codepoint = 0;
    for (int i = 0; i < 4; ++i) {
      const char ch = text_[pos_++];
      codepoint <<= 4;
      if (ch >= '0' && ch <= '9') codepoint += static_cast<uint32_t>(ch - '0');
      else if (ch >= 'a' && ch <= 'f') codepoint += static_cast<uint32_t>(ch - 'a' + 10);
      else if (ch >= 'A' && ch <= 'F') codepoint += static_cast<uint32_t>(ch - 'A' + 10);
      else fail("invalid JSON unicode escape");
    }
    return encodeUtf8(codepoint);
  }

  std::string encodeUtf8(uint32_t codepoint) {
    std::string out;
    if (codepoint <= 0x7F) {
      out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
      out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
      out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
      out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    return out;
  }

  JsonValue parseArray() {
    expect('[');
    JsonValue::Array array;
    skipSpace();
    if (consume(']')) return JsonValue(std::move(array));
    while (true) {
      array.push_back(parseValue());
      skipSpace();
      if (consume(']')) break;
      expect(',');
    }
    return JsonValue(std::move(array));
  }

  JsonValue parseObject() {
    expect('{');
    JsonValue::Object object;
    skipSpace();
    if (consume('}')) return JsonValue(std::move(object));
    while (true) {
      skipSpace();
      if (peek() != '"') fail("JSON object key must be a string");
      std::string key = parseString();
      skipSpace();
      expect(':');
      object.emplace(std::move(key), parseValue());
      skipSpace();
      if (consume('}')) break;
      expect(',');
    }
    return JsonValue(std::move(object));
  }

  JsonValue parseNumber() {
    const size_t begin = pos_;
    consume('-');
    if (consume('0')) {
      if (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) fail("leading zero in JSON number");
    } else {
      consumeDigits("missing JSON number integer part");
    }
    if (consume('.')) consumeDigits("missing JSON number fraction");
    if (consume('e') || consume('E')) {
      consume('+') || consume('-');
      consumeDigits("missing JSON number exponent");
    }
    return JsonValue::number(text_.substr(begin, pos_ - begin));
  }

  void consumeDigits(const char* error_message) {
    const size_t begin = pos_;
    while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
    if (begin == pos_) fail(error_message);
  }

  void skipSpace() {
    while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
  }

  char peek() const {
    return pos_ < text_.size() ? text_[pos_] : '\0';
  }

  bool consume(char expected) {
    if (pos_ < text_.size() && text_[pos_] == expected) {
      ++pos_;
      return true;
    }
    return false;
  }

  void expect(char expected) {
    if (!consume(expected)) {
      std::string message = "expected JSON character '";
      message.push_back(expected);
      message.push_back('\'');
      fail(message);
    }
  }

  [[noreturn]] void fail(const std::string& message) const {
    std::ostringstream out;
    out << message << " at byte " << pos_;
    throw JsonParseError(out.str());
  }

  const std::string& text_;
  size_t pos_ = 0;
};

void dumpString(std::ostringstream& out, const std::string& value) {
  out << '"';
  for (const unsigned char ch : value) {
    switch (ch) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\b': out << "\\b"; break;
      case '\f': out << "\\f"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (ch < 0x20) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch)
              << std::dec << std::setfill(' ');
        } else {
          out << static_cast<char>(ch);
        }
        break;
    }
  }
  out << '"';
}

} // namespace

JsonParseError::JsonParseError(const std::string& message) : std::runtime_error(message) {}

JsonValue::JsonValue() : type_(Type::kNull) {}

JsonValue::JsonValue(std::nullptr_t) : type_(Type::kNull) {}

JsonValue::JsonValue(bool value) : type_(Type::kBool), bool_value_(value) {}

JsonValue::JsonValue(int value) : type_(Type::kNumber), scalar_(std::to_string(value)) {}

JsonValue::JsonValue(uint32_t value) : type_(Type::kNumber), scalar_(std::to_string(value)) {}

JsonValue::JsonValue(uint64_t value) : type_(Type::kNumber), scalar_(std::to_string(value)) {}

JsonValue::JsonValue(double value) : type_(Type::kNumber) {
  if (!std::isfinite(value)) throw std::invalid_argument("JSON number must be finite");
  std::ostringstream out;
  out << std::setprecision(std::numeric_limits<double>::digits10 + 1) << value;
  scalar_ = out.str();
}

JsonValue::JsonValue(const char* value) : JsonValue(std::string(value ? value : "")) {}

JsonValue::JsonValue(std::string value) : type_(Type::kString), scalar_(std::move(value)) {}

JsonValue::JsonValue(Array value) : type_(Type::kArray), array_(std::move(value)) {}

JsonValue::JsonValue(Object value) : type_(Type::kObject), object_(std::move(value)) {}

JsonValue::JsonValue(Type type) : type_(type) {}

JsonValue JsonValue::array() {
  return JsonValue(Array{});
}

JsonValue JsonValue::object() {
  return JsonValue(Object{});
}

JsonValue JsonValue::number(std::string raw_number) {
  JsonValue value(Type::kNumber);
  value.scalar_ = std::move(raw_number);
  return value;
}

JsonValue::Type JsonValue::type() const {
  return type_;
}

bool JsonValue::isNull() const {
  return type_ == Type::kNull;
}

bool JsonValue::isBool() const {
  return type_ == Type::kBool;
}

bool JsonValue::isNumber() const {
  return type_ == Type::kNumber;
}

bool JsonValue::isString() const {
  return type_ == Type::kString;
}

bool JsonValue::isArray() const {
  return type_ == Type::kArray;
}

bool JsonValue::isObject() const {
  return type_ == Type::kObject;
}

bool JsonValue::asBool() const {
  if (!isBool()) throw std::logic_error("JSON value is not a bool");
  return bool_value_;
}

int64_t JsonValue::asInt64() const {
  if (!isNumber()) throw std::logic_error("JSON value is not a number");
  size_t consumed = 0;
  const auto value = std::stoll(scalar_, &consumed);
  if (consumed != scalar_.size()) throw std::logic_error("JSON number is not an integer");
  return value;
}

uint64_t JsonValue::asUInt64() const {
  if (!isNumber()) throw std::logic_error("JSON value is not a number");
  if (!scalar_.empty() && scalar_[0] == '-') throw std::logic_error("JSON number is negative");
  size_t consumed = 0;
  const auto value = std::stoull(scalar_, &consumed);
  if (consumed != scalar_.size()) throw std::logic_error("JSON number is not an unsigned integer");
  return value;
}

double JsonValue::asDouble() const {
  if (!isNumber()) throw std::logic_error("JSON value is not a number");
  return std::stod(scalar_);
}

const std::string& JsonValue::asString() const {
  if (!isString()) throw std::logic_error("JSON value is not a string");
  return scalar_;
}

const JsonValue::Array& JsonValue::asArray() const {
  if (!isArray()) throw std::logic_error("JSON value is not an array");
  return array_;
}

const JsonValue::Object& JsonValue::asObject() const {
  if (!isObject()) throw std::logic_error("JSON value is not an object");
  return object_;
}

JsonValue::Array& JsonValue::asArray() {
  if (!isArray()) throw std::logic_error("JSON value is not an array");
  return array_;
}

JsonValue::Object& JsonValue::asObject() {
  if (!isObject()) throw std::logic_error("JSON value is not an object");
  return object_;
}

bool JsonValue::has(const std::string& key) const {
  return isObject() && object_.find(key) != object_.end();
}

const JsonValue& JsonValue::at(const std::string& key) const {
  if (!isObject()) throw std::logic_error("JSON value is not an object");
  const auto it = object_.find(key);
  if (it == object_.end()) throw std::out_of_range("JSON object key is missing: " + key);
  return it->second;
}

JsonValue& JsonValue::operator[](const std::string& key) {
  if (isNull()) type_ = Type::kObject;
  if (!isObject()) throw std::logic_error("JSON value is not an object");
  return object_[key];
}

void JsonValue::pushBack(JsonValue value) {
  if (isNull()) type_ = Type::kArray;
  if (!isArray()) throw std::logic_error("JSON value is not an array");
  array_.push_back(std::move(value));
}

std::string JsonValue::dump() const {
  std::ostringstream out;
  switch (type_) {
    case Type::kNull:
      out << "null";
      break;
    case Type::kBool:
      out << (bool_value_ ? "true" : "false");
      break;
    case Type::kNumber:
      out << scalar_;
      break;
    case Type::kString:
      dumpString(out, scalar_);
      break;
    case Type::kArray:
      out << '[';
      for (size_t i = 0; i < array_.size(); ++i) {
        if (i > 0) out << ',';
        out << array_[i].dump();
      }
      out << ']';
      break;
    case Type::kObject:
      out << '{';
      for (auto it = object_.begin(); it != object_.end(); ++it) {
        if (it != object_.begin()) out << ',';
        dumpString(out, it->first);
        out << ':' << it->second.dump();
      }
      out << '}';
      break;
  }
  return out.str();
}

JsonValue parseJson(const std::string& text) {
  return Parser(text).parse();
}

std::string escapeJsonString(const std::string& value) {
  std::ostringstream out;
  dumpString(out, value);
  return out.str();
}

} // namespace audio_studio::rpc
