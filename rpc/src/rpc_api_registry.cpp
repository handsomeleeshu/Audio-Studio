#include "rpc_api_registry.hpp"

#include <utility>

#include "rpc_runtime_context.hpp"

namespace audio_studio::rpc {

bool RpcApiRegistry::addMethod(RpcMethodSpec spec) {
  if (spec.method.empty() || !spec.handler) return false;
  return methods_.emplace(spec.method, std::move(spec)).second;
}

bool RpcApiRegistry::hasMethod(const std::string& method) const {
  return methods_.find(method) != methods_.end();
}

const RpcMethodSpec* RpcApiRegistry::findMethod(const std::string& method) const {
  const auto it = methods_.find(method);
  return it == methods_.end() ? nullptr : &it->second;
}

std::vector<RpcMethodSpec> RpcApiRegistry::methods() const {
  std::vector<RpcMethodSpec> out;
  out.reserve(methods_.size());
  for (const auto& item : methods_) out.push_back(item.second);
  return out;
}

std::vector<RpcMethodSpec> RpcApiRegistry::smokeTests() const {
  std::vector<RpcMethodSpec> out;
  for (const auto& item : methods_) {
    if (item.second.smoke_test.enabled) out.push_back(item.second);
  }
  return out;
}

std::string RpcApiRegistry::defaultMethodForTool(const std::string& tool, const std::string& action) const {
  for (const auto& item : methods_) {
    const auto& binding = item.second.cli;
    bool tool_matches = false;
    for (const auto& candidate : binding.tools) {
      if (candidate == tool) {
        tool_matches = true;
        break;
      }
    }
    if (!tool_matches) continue;
    if (binding.default_action.empty() || binding.default_action == action || action.empty()) return item.first;
  }
  return "server.health";
}

JsonValue RpcApiRegistry::describeMethod(const std::string& method) const {
  const auto* spec = findMethod(method);
  if (spec == nullptr) throw JsonRpcError(JsonRpcErrorCode::kMethodNotFound, "method not found: " + method);

  JsonValue value = JsonValue::object();
  value["method"] = spec->method;
  value["summary"] = spec->summary;
  value["service"] = spec->service;
  value["version"] = spec->version;
  value["params_example"] = spec->params_example;
  value["result_example"] = spec->result_example;
  return value;
}

JsonValue RpcApiRegistry::listMethods() const {
  JsonValue methods = JsonValue::array();
  for (const auto& item : methods_) {
    JsonValue value = JsonValue::object();
    value["method"] = item.second.method;
    value["summary"] = item.second.summary;
    value["service"] = item.second.service;
    value["version"] = item.second.version;
    methods.pushBack(std::move(value));
  }
  JsonValue result = JsonValue::object();
  result["methods"] = std::move(methods);
  return result;
}

void RpcApiRegistry::registerEndpoint(JsonRpcEndpoint& endpoint, RpcRuntimeContextPtr context) const {
  if (!context) throw JsonRpcError(JsonRpcErrorCode::kInternalError, "RPC runtime context is null");
  for (const auto& item : methods_) {
    const auto spec = item.second;
    endpoint.addMethod(spec.method, [context, spec](const JsonValue& params) {
      return spec.handler(*context, params);
    });
  }
}

} // namespace audio_studio::rpc
