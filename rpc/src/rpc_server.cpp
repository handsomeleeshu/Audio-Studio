#include "audio_studio/rpc/rpc_server.hpp"

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

} // namespace audio_studio::rpc
