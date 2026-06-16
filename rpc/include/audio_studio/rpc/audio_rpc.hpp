#pragma once

#include "audio_studio/framework/audio/audio_service.hpp"
#include "audio_studio/rpc/json_rpc.hpp"

namespace audio_studio::rpc {

void registerAudioRpcMethods(JsonRpcEndpoint& endpoint, framework::audio::AudioService& audio_service);

} // namespace audio_studio::rpc
