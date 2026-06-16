#include "rpc_runtime_context.hpp"

#include <utility>

#include "audio_studio/framework/audio/audio_service.hpp"

namespace audio_studio::rpc {

RpcRuntimeContext::RpcRuntimeContext(framework::audio::AudioService& audio_service)
  : audio_service_(audio_service) {}

RpcRuntimeContext::RpcRuntimeContext(framework::audio::AudioService& audio_service, RpcStreamDefaults stream_defaults)
  : audio_service_(audio_service), stream_defaults_(std::move(stream_defaults)) {}

framework::audio::AudioService& RpcRuntimeContext::audio() {
  return audio_service_;
}

const RpcStreamDefaults& RpcRuntimeContext::streamDefaults() const {
  return stream_defaults_;
}

void RpcRuntimeContext::setStreamDefaults(RpcStreamDefaults defaults) {
  stream_defaults_ = std::move(defaults);
}

std::string RpcRuntimeContext::nextSessionId(const std::string& prefix) {
  return prefix + "_" + std::to_string(next_id_.fetch_add(1));
}

uint32_t RpcRuntimeContext::numericSessionId(const std::string& session_id) {
  std::lock_guard<std::mutex> lock(ids_mutex_);
  auto it = session_ids_.find(session_id);
  if (it != session_ids_.end()) return it->second;
  const uint32_t id = next_id_.fetch_add(1);
  session_ids_.emplace(session_id, id);
  return id;
}

uint32_t RpcRuntimeContext::numericStreamId(const std::string& session_id) {
  std::lock_guard<std::mutex> lock(ids_mutex_);
  auto it = stream_ids_.find(session_id);
  if (it != stream_ids_.end()) return it->second;
  const uint32_t id = next_id_.fetch_add(1);
  stream_ids_.emplace(session_id, id);
  return id;
}

void RpcRuntimeContext::releaseSession(const std::string& session_id) {
  std::lock_guard<std::mutex> lock(ids_mutex_);
  session_ids_.erase(session_id);
  stream_ids_.erase(session_id);
}

} // namespace audio_studio::rpc
