#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

#include "audio_studio/rpc/json_value.hpp"

namespace audio_studio::rpc {

enum class JsonRpcErrorCode {
  kParseError = -32700,
  kInvalidRequest = -32600,
  kMethodNotFound = -32601,
  kInvalidParams = -32602,
  kInternalError = -32603,
};

class JsonRpcError : public std::runtime_error {
public:
  JsonRpcError(JsonRpcErrorCode code, const std::string& message);

  JsonRpcErrorCode code() const;
  int numericCode() const;
  const std::string& message() const;

private:
  JsonRpcErrorCode code_;
  std::string message_;
};

class JsonRpcEndpoint {
public:
  using Method = std::function<JsonValue(const JsonValue& params)>;

  bool addMethod(const std::string& name, Method method);
  bool hasMethod(const std::string& name) const;
  std::string handleRequest(const std::string& request_json) const;

private:
  JsonValue handleSingleRequest(const JsonValue& request, bool& has_response) const;
  JsonValue invoke(const std::string& method, const JsonValue& params) const;

  std::map<std::string, Method> methods_;
};

class IJsonRpcTransport {
public:
  virtual ~IJsonRpcTransport() = default;
  virtual std::string send(const std::string& request_json) = 0;
};

class InProcessJsonRpcTransport final : public IJsonRpcTransport {
public:
  explicit InProcessJsonRpcTransport(const JsonRpcEndpoint& endpoint);
  std::string send(const std::string& request_json) override;

private:
  const JsonRpcEndpoint& endpoint_;
};

class JsonRpcClient {
public:
  explicit JsonRpcClient(IJsonRpcTransport& transport);

  JsonValue call(const std::string& method, const JsonValue& params = JsonValue::object());
  void notify(const std::string& method, const JsonValue& params = JsonValue::object());

private:
  JsonValue buildRequest(const std::string& method, const JsonValue& params, bool notification);

  IJsonRpcTransport& transport_;
  uint64_t next_id_ = 1;
};

JsonValue requireObjectParams(const JsonValue& params, const std::string& method);
std::string requireStringParam(const JsonValue& params, const std::string& name);
uint32_t optionalUInt32Param(const JsonValue& params, const std::string& name, uint32_t fallback);
uint16_t optionalUInt16Param(const JsonValue& params, const std::string& name, uint16_t fallback);

} // namespace audio_studio::rpc
