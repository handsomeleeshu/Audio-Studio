#include "rpc_runtime_context.hpp"

#include <stdexcept>
#include <utility>

#include "audio_service.hpp"

namespace audio_studio::rpc {

RpcRuntimeContext::RpcRuntimeContext(framework::audio::AudioService& audio_service)
  : audio_service_(audio_service) {}

RpcRuntimeContext::RpcRuntimeContext(framework::audio::AudioService& audio_service, RpcStreamDefaults stream_defaults)
  : audio_service_(audio_service), stream_defaults_(std::move(stream_defaults)) {}

framework::audio::AudioService& RpcRuntimeContext::audio() {
  return audio_service_;
}

void RpcRuntimeContext::setConfigService(framework::config::ConfigService* config_service) {
  config_service_ = config_service;
}

bool RpcRuntimeContext::hasConfigService() const {
  return config_service_ != nullptr;
}

framework::config::ConfigService& RpcRuntimeContext::config() {
  if (config_service_ == nullptr) throw std::runtime_error("config service is not configured");
  return *config_service_;
}

void RpcRuntimeContext::setLogService(framework::log::LogService* log_service) {
  log_service_ = log_service;
}

bool RpcRuntimeContext::hasLogService() const {
  return log_service_ != nullptr;
}

framework::log::LogService& RpcRuntimeContext::log() {
  if (log_service_ == nullptr) throw std::runtime_error("log service is not configured");
  return *log_service_;
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
  numeric_sessions_.emplace(id, session_id);
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
  const auto id = session_ids_.find(session_id);
  if (id != session_ids_.end()) numeric_sessions_.erase(id->second);
  session_ids_.erase(session_id);
  stream_ids_.erase(session_id);
}

bool RpcRuntimeContext::sessionIdForNumeric(uint32_t numeric_session_id, std::string& session_id) const {
  std::lock_guard<std::mutex> lock(ids_mutex_);
  const auto it = numeric_sessions_.find(numeric_session_id);
  if (it == numeric_sessions_.end()) return false;
  session_id = it->second;
  return true;
}

} // namespace audio_studio::rpc
