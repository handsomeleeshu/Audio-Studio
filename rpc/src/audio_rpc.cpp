#include "audio_rpc.hpp"

#include <memory>

namespace audio_studio::rpc {

void registerAudioRpcMethods(JsonRpcEndpoint& endpoint, framework::audio::AudioService& audio_service) {
  auto context = std::make_shared<RpcRuntimeContext>(audio_service);
  registerAudioStudioRpcMethods(endpoint, context);
}

} // namespace audio_studio::rpc
