#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace audio_studio::framework::audio {
class AudioService;
}

namespace audio_studio::framework::config {
class ConfigService;
}

namespace audio_studio::framework::log {
class LogService;
}

namespace audio_studio::framework::system_info {
class SystemInfoService;
}

namespace audio_studio::rpc {

struct RpcStreamDefaults {
  std::string host = "127.0.0.1";
  uint16_t port = 9900;
  uint32_t max_chunk_bytes = 65536;
  uint32_t timeout_ms = 5000;
  std::string stream_uri_base;
  std::string socket_scheme = "tcp";
  std::string pipe_scheme = "pipe";
};

class RpcRuntimeContext {
public:
  explicit RpcRuntimeContext(framework::audio::AudioService& audio_service);
  RpcRuntimeContext(framework::audio::AudioService& audio_service, RpcStreamDefaults stream_defaults);

  framework::audio::AudioService& audio();
  void setConfigService(framework::config::ConfigService* config_service);
  bool hasConfigService() const;
  framework::config::ConfigService& config();
  void setLogService(framework::log::LogService* log_service);
  bool hasLogService() const;
  framework::log::LogService& log();
  void setSystemInfoService(framework::system_info::SystemInfoService* system_info_service);
  bool hasSystemInfoService() const;
  framework::system_info::SystemInfoService& systemInfo();
  const RpcStreamDefaults& streamDefaults() const;
  void setStreamDefaults(RpcStreamDefaults defaults);

  std::string nextSessionId(const std::string& prefix);
  uint32_t numericSessionId(const std::string& session_id);
  uint32_t numericStreamId(const std::string& session_id);
  bool sessionIdForNumeric(uint32_t numeric_session_id, std::string& session_id) const;
  void releaseSession(const std::string& session_id);

private:
  framework::audio::AudioService& audio_service_;
  framework::config::ConfigService* config_service_ = nullptr;
  framework::log::LogService* log_service_ = nullptr;
  framework::system_info::SystemInfoService* system_info_service_ = nullptr;
  RpcStreamDefaults stream_defaults_;
  std::atomic<uint32_t> next_id_ {1};
  mutable std::mutex ids_mutex_;
  std::map<std::string, uint32_t> session_ids_;
  std::map<std::string, uint32_t> stream_ids_;
  std::map<uint32_t, std::string> numeric_sessions_;
};

} // namespace audio_studio::rpc
