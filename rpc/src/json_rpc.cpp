#include "audio_studio/rpc/json_rpc.hpp"

#include <exception>
#include <sstream>
#include <utility>

namespace audio_studio::rpc {
namespace {

JsonValue nullId() {
  return JsonValue();
}

bool validId(const JsonValue& id) {
  return id.isNull() || id.isString() || id.isNumber();
}

JsonValue errorObject(int code, const std::string& message) {
  JsonValue error = JsonValue::object();
  error["code"] = code;
  error["message"] = message;
  return error;
}

JsonValue errorResponse(const JsonValue& id, int code, const std::string& message) {
  JsonValue response = JsonValue::object();
  response["jsonrpc"] = "2.0";
  response["id"] = id;
  response["error"] = errorObject(code, message);
  return response;
}

JsonValue resultResponse(const JsonValue& id, JsonValue result) {
  JsonValue response = JsonValue::object();
  response["jsonrpc"] = "2.0";
  response["id"] = id;
  response["result"] = std::move(result);
  return response;
}

JsonValue invalidRequest(const std::string& message) {
  return errorResponse(nullId(), static_cast<int>(JsonRpcErrorCode::kInvalidRequest), message);
}

JsonValue responseIdFromRequest(const JsonValue& request) {
  if (!request.isObject() || !request.has("id")) return nullId();
  const JsonValue& id = request.at("id");
  return validId(id) ? id : nullId();
}

} // namespace

JsonRpcError::JsonRpcError(JsonRpcErrorCode code, const std::string& message)
  : std::runtime_error(message), code_(code), message_(message) {}

JsonRpcErrorCode JsonRpcError::code() const {
  return code_;
}

int JsonRpcError::numericCode() const {
  return static_cast<int>(code_);
}

const std::string& JsonRpcError::message() const {
  return message_;
}

bool JsonRpcEndpoint::addMethod(const std::string& name, Method method) {
  if (name.empty() || !method) return false;
  if (name.rfind("rpc.", 0) == 0) return false;
  return methods_.emplace(name, std::move(method)).second;
}

bool JsonRpcEndpoint::hasMethod(const std::string& name) const {
  return methods_.find(name) != methods_.end();
}

std::string JsonRpcEndpoint::handleRequest(const std::string& request_json) const {
  JsonValue request;
  try {
    request = parseJson(request_json);
  } catch (const JsonParseError& error) {
    return errorResponse(nullId(), static_cast<int>(JsonRpcErrorCode::kParseError),
                         std::string("parse error: ") + error.what()).dump();
  }

  if (request.isArray()) {
    if (request.asArray().empty()) {
      return invalidRequest("invalid request: empty batch").dump();
    }
    JsonValue batch = JsonValue::array();
    for (const auto& item : request.asArray()) {
      bool has_response = true;
      JsonValue response = handleSingleRequest(item, has_response);
      if (has_response) batch.pushBack(std::move(response));
    }
    return batch.asArray().empty() ? std::string() : batch.dump();
  }

  bool has_response = true;
  JsonValue response = handleSingleRequest(request, has_response);
  return has_response ? response.dump() : std::string();
}

JsonValue JsonRpcEndpoint::handleSingleRequest(const JsonValue& request, bool& has_response) const {
  has_response = true;
  if (!request.isObject()) return invalidRequest("invalid request: expected object");

  const JsonValue response_id = responseIdFromRequest(request);
  if (!request.has("jsonrpc") || !request.at("jsonrpc").isString() || request.at("jsonrpc").asString() != "2.0") {
    return errorResponse(response_id, static_cast<int>(JsonRpcErrorCode::kInvalidRequest),
                         "invalid request: jsonrpc must be \"2.0\"");
  }
  if (!request.has("method") || !request.at("method").isString()) {
    return errorResponse(response_id, static_cast<int>(JsonRpcErrorCode::kInvalidRequest),
                         "invalid request: method must be a string");
  }
  if (request.has("id") && !validId(request.at("id"))) {
    return errorResponse(nullId(), static_cast<int>(JsonRpcErrorCode::kInvalidRequest),
                         "invalid request: id must be string, number, or null");
  }

  JsonValue params = JsonValue::object();
  if (request.has("params")) {
    params = request.at("params");
    if (!(params.isObject() || params.isArray() || params.isNull())) {
      return errorResponse(response_id, static_cast<int>(JsonRpcErrorCode::kInvalidRequest),
                           "invalid request: params must be object, array, or null");
    }
    if (params.isNull()) params = JsonValue::object();
  }

  try {
    JsonValue result = invoke(request.at("method").asString(), params);
    if (!request.has("id")) {
      has_response = false;
      return JsonValue();
    }
    return resultResponse(request.at("id"), std::move(result));
  } catch (const JsonRpcError& error) {
    if (!request.has("id")) {
      has_response = false;
      return JsonValue();
    }
    return errorResponse(response_id, error.numericCode(), error.message());
  } catch (const std::exception& error) {
    if (!request.has("id")) {
      has_response = false;
      return JsonValue();
    }
    return errorResponse(response_id, static_cast<int>(JsonRpcErrorCode::kInternalError),
                         std::string("internal error: ") + error.what());
  } catch (...) {
    if (!request.has("id")) {
      has_response = false;
      return JsonValue();
    }
    return errorResponse(response_id, static_cast<int>(JsonRpcErrorCode::kInternalError), "internal error");
  }
}

JsonValue JsonRpcEndpoint::invoke(const std::string& method, const JsonValue& params) const {
  const auto it = methods_.find(method);
  if (it == methods_.end()) {
    throw JsonRpcError(JsonRpcErrorCode::kMethodNotFound, "method not found: " + method);
  }
  return it->second(params);
}

InProcessJsonRpcTransport::InProcessJsonRpcTransport(const JsonRpcEndpoint& endpoint) : endpoint_(endpoint) {}

std::string InProcessJsonRpcTransport::send(const std::string& request_json) {
  return endpoint_.handleRequest(request_json);
}

JsonRpcClient::JsonRpcClient(IJsonRpcTransport& transport) : transport_(transport) {}

JsonValue JsonRpcClient::call(const std::string& method, const JsonValue& params) {
  JsonValue request = buildRequest(method, params, false);
  JsonValue response = parseJson(transport_.send(request.dump()));
  if (response.has("error")) {
    const JsonValue& error = response.at("error");
    const int code = static_cast<int>(error.at("code").asInt64());
    throw JsonRpcError(static_cast<JsonRpcErrorCode>(code), error.at("message").asString());
  }
  if (!response.has("result")) {
    throw JsonRpcError(JsonRpcErrorCode::kInternalError, "invalid JSON-RPC response: missing result");
  }
  return response.at("result");
}

void JsonRpcClient::notify(const std::string& method, const JsonValue& params) {
  JsonValue request = buildRequest(method, params, true);
  (void)transport_.send(request.dump());
}

JsonValue JsonRpcClient::buildRequest(const std::string& method, const JsonValue& params, bool notification) {
  JsonValue request = JsonValue::object();
  request["jsonrpc"] = "2.0";
  request["method"] = method;
  if (!notification) request["id"] = next_id_++;
  if (!params.isNull()) request["params"] = params;
  return request;
}

JsonValue requireObjectParams(const JsonValue& params, const std::string& method) {
  if (!params.isObject()) {
    throw JsonRpcError(JsonRpcErrorCode::kInvalidParams, method + " expects named object params");
  }
  return params;
}

std::string requireStringParam(const JsonValue& params, const std::string& name) {
  const auto object = requireObjectParams(params, name).asObject();
  const auto it = object.find(name);
  if (it == object.end() || !it->second.isString()) {
    throw JsonRpcError(JsonRpcErrorCode::kInvalidParams, "missing string param: " + name);
  }
  return it->second.asString();
}

uint32_t optionalUInt32Param(const JsonValue& params, const std::string& name, uint32_t fallback) {
  if (!params.isObject() || !params.has(name)) return fallback;
  const JsonValue& value = params.at(name);
  if (!value.isNumber()) throw JsonRpcError(JsonRpcErrorCode::kInvalidParams, "param must be number: " + name);
  return static_cast<uint32_t>(value.asUInt64());
}

uint16_t optionalUInt16Param(const JsonValue& params, const std::string& name, uint16_t fallback) {
  if (!params.isObject() || !params.has(name)) return fallback;
  const JsonValue& value = params.at(name);
  if (!value.isNumber()) throw JsonRpcError(JsonRpcErrorCode::kInvalidParams, "param must be number: " + name);
  return static_cast<uint16_t>(value.asUInt64());
}

} // namespace audio_studio::rpc
