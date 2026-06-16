#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "json_rpc.hpp"

namespace audio_studio::rpc {

class RpcRuntimeContext;
using RpcRuntimeContextPtr = std::shared_ptr<RpcRuntimeContext>;

struct RpcCliBinding {
  std::vector<std::string> tools;
  std::string default_action;
};

struct RpcSmokeTest {
  bool enabled = false;
  JsonValue params = JsonValue::object();
};

struct RpcMethodSpec {
  std::string method;
  std::string summary;
  std::string service;
  std::string version = "1.0";
  JsonValue params_example = JsonValue::object();
  JsonValue result_example = JsonValue::object();
  RpcCliBinding cli;
  RpcSmokeTest smoke_test;
  std::function<JsonValue(RpcRuntimeContext& context, const JsonValue& params)> handler;
};

class RpcApiRegistry {
public:
  bool addMethod(RpcMethodSpec spec);
  bool hasMethod(const std::string& method) const;
  const RpcMethodSpec* findMethod(const std::string& method) const;
  std::vector<RpcMethodSpec> methods() const;
  std::vector<RpcMethodSpec> smokeTests() const;

  std::string defaultMethodForTool(const std::string& tool, const std::string& action) const;
  JsonValue describeMethod(const std::string& method) const;
  JsonValue listMethods() const;
  void registerEndpoint(JsonRpcEndpoint& endpoint, RpcRuntimeContextPtr context) const;

private:
  std::map<std::string, RpcMethodSpec> methods_;
};

RpcApiRegistry& audioStudioRpcApiRegistry();
void registerAudioStudioRpcMethods(JsonRpcEndpoint& endpoint, RpcRuntimeContextPtr context);

} // namespace audio_studio::rpc
