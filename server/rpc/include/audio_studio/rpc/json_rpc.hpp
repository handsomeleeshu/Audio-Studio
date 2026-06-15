#pragma once

#include <string>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::rpc {

struct JsonRpcRequest {
  std::string id;
  std::string method;
  std::string params_json;
};

framework::Status parseRequest(const std::string& json, JsonRpcRequest& out);
std::string resultResponse(const std::string& id, const std::string& result_json);
std::string errorResponse(const std::string& id, int code, const std::string& message);

} // namespace audio_studio::rpc
