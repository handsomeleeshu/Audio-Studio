#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace audio_studio::rpc {

class JsonParseError : public std::runtime_error {
public:
  explicit JsonParseError(const std::string& message);
};

class JsonValue {
public:
  enum class Type {
    kNull,
    kBool,
    kNumber,
    kString,
    kArray,
    kObject,
  };

  using Array = std::vector<JsonValue>;
  using Object = std::map<std::string, JsonValue>;

  JsonValue();
  JsonValue(std::nullptr_t);
  JsonValue(bool value);
  JsonValue(int value);
  JsonValue(uint32_t value);
  JsonValue(uint64_t value);
  JsonValue(double value);
  JsonValue(const char* value);
  JsonValue(std::string value);
  JsonValue(Array value);
  JsonValue(Object value);

  static JsonValue array();
  static JsonValue object();
  static JsonValue number(std::string raw_number);

  Type type() const;
  bool isNull() const;
  bool isBool() const;
  bool isNumber() const;
  bool isString() const;
  bool isArray() const;
  bool isObject() const;

  bool asBool() const;
  int64_t asInt64() const;
  uint64_t asUInt64() const;
  double asDouble() const;
  const std::string& asString() const;
  const Array& asArray() const;
  const Object& asObject() const;
  Array& asArray();
  Object& asObject();

  bool has(const std::string& key) const;
  const JsonValue& at(const std::string& key) const;
  JsonValue& operator[](const std::string& key);
  void pushBack(JsonValue value);

  std::string dump() const;

private:
  explicit JsonValue(Type type);

  Type type_;
  bool bool_value_ = false;
  std::string scalar_;
  Array array_;
  Object object_;
};

JsonValue parseJson(const std::string& text);
std::string escapeJsonString(const std::string& value);

} // namespace audio_studio::rpc
