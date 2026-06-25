#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "log_service.hpp"

namespace audio_studio::framework::system_info {

inline constexpr const char* kAudioStudioInfoPrefix = "ASINFO|";

struct SystemCoreInfo {
  int id = 0;
  double freq_mhz = 0.0;
  double load_percent = 0.0;
};

struct SystemModuleInfo {
  std::string node_id;
  uint32_t pipeline_id = 0;
  std::string state;
  int core = 0;
  double cpu_percent = 0.0;
  uint64_t memory_bytes = 0;
  double latency_ms = 0.0;
};

struct SystemBufferInfo {
  std::string edge_key;
  std::string from;
  std::string to;
  uint64_t size_bytes = 0;
  uint64_t avail_bytes = 0;
  uint64_t produced_bytes = 0;
  uint64_t consumed_bytes = 0;
  bool stalled = false;
};

struct SystemHeapInfo {
  std::string category;
  int index = 0;
  uint32_t block_size = 0;
  uint32_t free_count = 0;
  uint32_t total_count = 0;
  uint64_t used_bytes = 0;
  uint64_t free_bytes = 0;
};

struct SystemPipelineInfo {
  uint32_t id = 0;
  double latency_ms = 0.0;
  uint32_t xruns = 0;
  uint32_t dropouts = 0;
};

struct SystemInfoSnapshot {
  bool connected = false;
  bool running = false;
  uint64_t sequence = 0;
  int64_t timestamp_ms = 0;
  std::vector<SystemCoreInfo> cores;
  std::vector<SystemModuleInfo> modules;
  std::vector<SystemBufferInfo> buffers;
  std::vector<SystemHeapInfo> heap;
  std::vector<SystemPipelineInfo> pipelines;
};

class SystemInfoService {
public:
  ~SystemInfoService();

  bool consumeLogEntry(const framework::log::LogEntry& entry);
  SystemInfoSnapshot snapshot();
  void clear();
  void setHeartbeatTimeoutForTesting(std::chrono::milliseconds timeout);
  framework::Status startLogPump(framework::log::LogService& log_service,
                                 framework::log::LogSessionConfig config);
  framework::Status stopLogPump();
  bool logPumpRunning() const;

private:
  using Fields = std::map<std::string, std::string>;

  bool applyRecordLocked(const std::string& record);
  void clearRuntimeLocked();
  bool staleLocked(std::chrono::steady_clock::time_point now) const;
  void logPumpLoop();

  static Fields parseFields(const std::string& payload);
  static std::string fieldString(const Fields& fields, const std::string& key, std::string fallback = {});
  static uint32_t fieldU32(const Fields& fields, const std::string& key, uint32_t fallback = 0);
  static uint64_t fieldU64(const Fields& fields, const std::string& key, uint64_t fallback = 0);
  static int fieldInt(const Fields& fields, const std::string& key, int fallback = 0);
  static double fieldDouble(const Fields& fields, const std::string& key, double fallback = 0.0);
  static int64_t nowMs();

  mutable std::mutex mutex_;
  std::chrono::milliseconds heartbeat_timeout_{1000};
  std::chrono::steady_clock::time_point last_update_{};
  SystemInfoSnapshot snapshot_;

  mutable std::mutex pump_mutex_;
  std::condition_variable pump_cv_;
  std::thread pump_thread_;
  framework::log::LogService* pump_log_service_ = nullptr;
  framework::log::LogSessionConfig pump_config_;
  std::string pump_session_id_;
  bool pump_running_ = false;
  bool pump_stop_requested_ = false;
};

} // namespace audio_studio::framework::system_info
