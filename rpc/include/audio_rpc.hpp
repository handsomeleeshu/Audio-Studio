#pragma once

#include "audio_studio/framework/audio/audio_service.hpp"
#include "json_rpc.hpp"
#include "rpc_api_registry.hpp"
#include "rpc_runtime_context.hpp"

namespace audio_studio::rpc {

void registerServerHealthRpcMethod(JsonRpcEndpoint& endpoint);
void registerAudioRpcMethods(JsonRpcEndpoint& endpoint, framework::audio::AudioService& audio_service);

} // namespace audio_studio::rpc
