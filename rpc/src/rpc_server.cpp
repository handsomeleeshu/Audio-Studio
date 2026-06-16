#include "rpc_server.hpp"

#include "autoconfig.h"

namespace audio_studio::rpc {
namespace {

const char* toolOs() {
#if defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#else
  return "linux";
#endif
}

const char* targetPlatform() {
#if defined(CONFIG_TARGET_PLATFORM_SIMULATOR)
  return "simulator";
#else
  return "a2";
#endif
}

} // namespace

void registerServerHealthRpcMethod(JsonRpcEndpoint& endpoint) {
  endpoint.addMethod("server.health", [](const JsonValue&) {
    JsonValue result = JsonValue::object();
    result["ok"] = true;
    result["tool_os"] = toolOs();
    result["platform"] = targetPlatform();
    return result;
  });
}

RpcBinaryFrame makeDefaultStreamAck(const RpcBinaryFrame& request) {
  JsonValue payload = JsonValue::object();
  payload["accepted"] = true;
  payload["accepted_bytes"] = static_cast<uint32_t>(request.payload.size());
  payload["queued_bytes"] = static_cast<uint32_t>(0);
  payload["credit_bytes"] = static_cast<uint32_t>(1024 * 1024);
  payload["request_id"] = request.header.request_id;

  const std::string json = payload.dump();
  RpcBinaryFrame ack;
  ack.header.message_type = RpcMessageType::kStreamAck;
  ack.header.service_id = request.header.service_id;
  ack.header.method_id = request.header.method_id;
  ack.header.payload_type = RpcPayloadType::kJson;
  ack.header.request_id = request.header.request_id;
  ack.header.session_id = request.header.session_id;
  ack.header.stream_id = request.header.stream_id;
  ack.payload.assign(json.begin(), json.end());
  return ack;
}

} // namespace audio_studio::rpc
