#include "system_info_service.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <thread>

namespace audio_studio::framework::system_info {
namespace {

bool startsWith(const std::string& value, const std::string& prefix) {
  return value.rfind(prefix, 0) == 0;
}

bool transientLogReadStatus(const framework::Status& status) {
  if (status.ok()) return true;
  const auto message = status.message();
  return message.find("no chunk") != std::string::npos ||
         message.find("no rv32qemu log chunk") != std::string::npos ||
         message.find("timed out") != std::string::npos;
}

std::string componentStateName(const std::string& value, const std::string& fallback) {
  if (value.empty()) return fallback.empty() ? "UNKNOWN" : fallback;

  char* end = nullptr;
  const auto numeric = std::strtol(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0') return value;

  switch (numeric) {
  case 0: return "NOT_EXIST";
  case 1: return "INIT";
  case 2: return "READY";
  case 3: return "SUSPEND";
  case 4: return "PREPARE";
  case 5: return "PAUSED";
  case 6: return "ACTIVE";
  case 7: return "PRE_ACTIVE";
  default: return "UNKNOWN";
  }
}

std::vector<std::string> splitPipe(const std::string& value) {
  std::vector<std::string> out;
  std::string item;
  std::istringstream input(value);
  while (std::getline(input, item, '|')) out.push_back(item);
  return out;
}

template <typename T, typename Predicate>
void upsert(std::vector<T>& values, const T& next, Predicate predicate) {
  auto it = std::find_if(values.begin(), values.end(), predicate);
  if (it == values.end()) values.push_back(next);
  else *it = next;
}

} // namespace

SystemInfoService::~SystemInfoService() {
  (void)stopLogPump();
}

bool SystemInfoService::consumeLogEntry(const framework::log::LogEntry& entry) {
  const auto parseCandidate = [this](const std::string& text) {
    const auto pos = text.find(kAudioStudioInfoPrefix);
    if (pos == std::string::npos) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    return applyRecordLocked(text.substr(pos));
  };

  if (parseCandidate(entry.message)) return true;
  if (entry.text != entry.message && parseCandidate(entry.text)) return true;
  return false;
}

SystemInfoSnapshot SystemInfoService::snapshot() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (staleLocked(std::chrono::steady_clock::now())) {
    clearRuntimeLocked();
  }
  return snapshot_;
}

void SystemInfoService::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  clearRuntimeLocked();
}

void SystemInfoService::setHeartbeatTimeoutForTesting(std::chrono::milliseconds timeout) {
  std::lock_guard<std::mutex> lock(mutex_);
  heartbeat_timeout_ = timeout;
}

framework::Status SystemInfoService::startLogPump(framework::log::LogService& log_service,
                                                  framework::log::LogSessionConfig config) {
  if (logPumpRunning()) {
    auto status = stopLogPump();
    if (!status.ok()) return status;
  }

  if (config.session_id.empty()) config.session_id = "system_info";
  if (config.min_level.empty()) config.min_level = "debug";
  config.options["system_info_pump"] = "true";

  {
    std::lock_guard<std::mutex> lock(pump_mutex_);
    pump_log_service_ = &log_service;
    pump_config_ = std::move(config);
    pump_session_id_ = pump_config_.session_id;
    pump_stop_requested_ = false;
    pump_running_ = true;
  }
  pump_thread_ = std::thread([this] { logPumpLoop(); });
  return framework::Status::success();
}

framework::Status SystemInfoService::stopLogPump() {
  {
    std::lock_guard<std::mutex> lock(pump_mutex_);
    if (!pump_running_) return framework::Status::success();
    pump_stop_requested_ = true;
  }
  pump_cv_.notify_all();
  if (pump_thread_.joinable()) pump_thread_.join();
  {
    std::lock_guard<std::mutex> lock(pump_mutex_);
    pump_log_service_ = nullptr;
    pump_config_ = {};
    pump_session_id_.clear();
    pump_stop_requested_ = false;
    pump_running_ = false;
  }
  return framework::Status::success();
}

bool SystemInfoService::logPumpRunning() const {
  std::lock_guard<std::mutex> lock(pump_mutex_);
  return pump_running_;
}

bool SystemInfoService::applyRecordLocked(const std::string& record) {
  if (!startsWith(record, kAudioStudioInfoPrefix)) return false;
  const std::string payload = record.substr(std::string(kAudioStudioInfoPrefix).size());
  const auto parts = splitPipe(payload);
  if (parts.empty() || parts.front().empty()) return false;

  const std::string type = parts.front();
  Fields fields;
  for (size_t i = 1; i < parts.size(); ++i) {
    const auto eq = parts[i].find('=');
    if (eq == std::string::npos) continue;
    fields[parts[i].substr(0, eq)] = parts[i].substr(eq + 1);
  }

  snapshot_.connected = true;
  snapshot_.running = true;
  snapshot_.timestamp_ms = nowMs();
  last_update_ = std::chrono::steady_clock::now();

  if (type == "heartbeat") {
    snapshot_.sequence = fieldU64(fields, "seq", snapshot_.sequence);
    const auto timestamp = fieldU64(fields, "timestamp_ms", 0);
    if (timestamp != 0) snapshot_.timestamp_ms = static_cast<int64_t>(timestamp);
    return true;
  }

  if (type == "core") {
    SystemCoreInfo core;
    core.id = fieldInt(fields, "id", 0);
    core.freq_mhz = fieldDouble(fields, "freq_mhz", 0.0);
    core.load_percent = fieldDouble(fields, "load_percent", 0.0);
    upsert(snapshot_.cores, core, [&](const SystemCoreInfo& item) { return item.id == core.id; });
    return true;
  }

  if (type == "module") {
    const std::string node_id = fieldString(fields, "id", fieldString(fields, "node_id"));
    SystemModuleInfo module;
    auto existing = std::find_if(snapshot_.modules.begin(), snapshot_.modules.end(),
                                 [&](const SystemModuleInfo& item) { return item.node_id == node_id; });
    if (existing != snapshot_.modules.end()) module = *existing;
    module.node_id = node_id;
    if (fields.find("pipeline") != fields.end()) module.pipeline_id = fieldU32(fields, "pipeline", module.pipeline_id);
    if (fields.find("state") != fields.end()) {
      module.state = componentStateName(fieldString(fields, "state"), module.state);
    }
    if (fields.find("core") != fields.end()) module.core = fieldInt(fields, "core", module.core);
    if (fields.find("cpu_percent") != fields.end()) module.cpu_percent = fieldDouble(fields, "cpu_percent", module.cpu_percent);
    if (fields.find("memory_bytes") != fields.end()) module.memory_bytes = fieldU64(fields, "memory_bytes", module.memory_bytes);
    if (fields.find("latency_ms") != fields.end()) module.latency_ms = fieldDouble(fields, "latency_ms", module.latency_ms);
    if (module.state.empty()) module.state = "UNKNOWN";
    if (module.node_id.empty()) return true;
    upsert(snapshot_.modules, module, [&](const SystemModuleInfo& item) {
      return item.node_id == module.node_id;
    });
    return true;
  }

  if (type == "buffer") {
    const std::string edge_key = fieldString(fields, "id", fieldString(fields, "edge_key"));
    SystemBufferInfo buffer;
    auto existing = std::find_if(snapshot_.buffers.begin(), snapshot_.buffers.end(),
                                 [&](const SystemBufferInfo& item) { return item.edge_key == edge_key; });
    if (existing != snapshot_.buffers.end()) buffer = *existing;
    buffer.edge_key = edge_key;
    if (fields.find("from") != fields.end()) buffer.from = fieldString(fields, "from", buffer.from);
    if (fields.find("to") != fields.end()) buffer.to = fieldString(fields, "to", buffer.to);
    if (fields.find("size_bytes") != fields.end()) buffer.size_bytes = fieldU64(fields, "size_bytes", buffer.size_bytes);
    if (fields.find("avail_bytes") != fields.end()) buffer.avail_bytes = fieldU64(fields, "avail_bytes", buffer.avail_bytes);
    if (fields.find("produced_bytes") != fields.end()) buffer.produced_bytes = fieldU64(fields, "produced_bytes", buffer.produced_bytes);
    if (fields.find("consumed_bytes") != fields.end()) buffer.consumed_bytes = fieldU64(fields, "consumed_bytes", buffer.consumed_bytes);
    if (fields.find("stalled") != fields.end()) buffer.stalled = fieldString(fields, "stalled", "0") == "1";
    if (buffer.edge_key.empty()) return true;
    upsert(snapshot_.buffers, buffer, [&](const SystemBufferInfo& item) {
      return item.edge_key == buffer.edge_key;
    });
    return true;
  }

  if (type == "heap") {
    SystemHeapInfo heap;
    heap.category = fieldString(fields, "category", "heap");
    heap.index = fieldInt(fields, "index", 0);
    heap.block_size = fieldU32(fields, "block_size", 0);
    auto existing = std::find_if(snapshot_.heap.begin(), snapshot_.heap.end(),
                                 [&](const SystemHeapInfo& item) {
                                   return item.category == heap.category &&
                                          item.index == heap.index &&
                                          item.block_size == heap.block_size;
                                 });
    if (existing != snapshot_.heap.end()) heap = *existing;
    if (fields.find("free_count") != fields.end()) heap.free_count = fieldU32(fields, "free_count", heap.free_count);
    if (fields.find("total_count") != fields.end()) heap.total_count = fieldU32(fields, "total_count", heap.total_count);
    if (fields.find("used_bytes") != fields.end()) heap.used_bytes = fieldU64(fields, "used_bytes", heap.used_bytes);
    if (fields.find("free_bytes") != fields.end()) heap.free_bytes = fieldU64(fields, "free_bytes", heap.free_bytes);
    upsert(snapshot_.heap, heap, [&](const SystemHeapInfo& item) {
      return item.category == heap.category &&
             item.index == heap.index &&
             item.block_size == heap.block_size;
    });
    return true;
  }

  if (type == "pipeline") {
    SystemPipelineInfo pipeline;
    pipeline.id = fieldU32(fields, "id", 0);
    pipeline.latency_ms = fieldDouble(fields, "latency_ms", 0.0);
    pipeline.xruns = fieldU32(fields, "xruns", 0);
    pipeline.dropouts = fieldU32(fields, "dropouts", 0);
    upsert(snapshot_.pipelines, pipeline, [&](const SystemPipelineInfo& item) {
      return item.id == pipeline.id;
    });
    return true;
  }

  return true;
}

void SystemInfoService::clearRuntimeLocked() {
  snapshot_ = {};
  last_update_ = {};
}

bool SystemInfoService::staleLocked(std::chrono::steady_clock::time_point now) const {
  return snapshot_.connected &&
         last_update_ != std::chrono::steady_clock::time_point{} &&
         now - last_update_ > heartbeat_timeout_;
}

void SystemInfoService::logPumpLoop() {
  bool session_active = false;
  framework::log::LogService* active_log_service = nullptr;
  std::string active_session_id;

  const auto close_active_session = [&] {
    if (session_active && active_log_service != nullptr && !active_session_id.empty()) {
      (void)active_log_service->stop(active_session_id);
      (void)active_log_service->closeSession(active_session_id);
    }
    session_active = false;
    active_log_service = nullptr;
    active_session_id.clear();
  };

  while (true) {
    framework::log::LogService* log_service = nullptr;
    framework::log::LogSessionConfig config;
    std::string session_id;
    {
      std::lock_guard<std::mutex> lock(pump_mutex_);
      if (pump_stop_requested_) break;
      log_service = pump_log_service_;
      config = pump_config_;
      session_id = pump_session_id_;
    }
    if (log_service == nullptr || session_id.empty()) break;

    if (!session_active) {
      framework::log::LogSessionInfo session;
      auto status = log_service->createSession(config, session);
      if (status.ok()) {
        status = log_service->start(session.session_id);
        if (status.ok()) {
          session_active = true;
          active_log_service = log_service;
          active_session_id = session.session_id;
        } else {
          (void)log_service->closeSession(session.session_id);
        }
      }
      if (!session_active) {
        std::unique_lock<std::mutex> lock(pump_mutex_);
        if (pump_cv_.wait_for(lock, std::chrono::milliseconds(100),
                              [&] { return pump_stop_requested_; })) {
          break;
        }
        continue;
      }
    }

    std::vector<framework::log::LogEntry> entries;
    const auto status = log_service->readEntries(active_session_id, 64, entries);
    if (!transientLogReadStatus(status)) {
      close_active_session();
    }
    const auto idle = !status.ok() || entries.empty();

    std::unique_lock<std::mutex> lock(pump_mutex_);
    if (pump_cv_.wait_for(lock, idle ? std::chrono::milliseconds(50) : std::chrono::milliseconds(1),
                          [&] { return pump_stop_requested_; })) {
      break;
    }
  }

  close_active_session();
}

SystemInfoService::Fields SystemInfoService::parseFields(const std::string& payload) {
  Fields fields;
  for (const auto& part : splitPipe(payload)) {
    const auto eq = part.find('=');
    if (eq == std::string::npos) continue;
    fields[part.substr(0, eq)] = part.substr(eq + 1);
  }
  return fields;
}

std::string SystemInfoService::fieldString(const Fields& fields, const std::string& key, std::string fallback) {
  const auto it = fields.find(key);
  return it == fields.end() ? std::move(fallback) : it->second;
}

uint32_t SystemInfoService::fieldU32(const Fields& fields, const std::string& key, uint32_t fallback) {
  return static_cast<uint32_t>(fieldU64(fields, key, fallback));
}

uint64_t SystemInfoService::fieldU64(const Fields& fields, const std::string& key, uint64_t fallback) {
  const auto it = fields.find(key);
  if (it == fields.end() || it->second.empty()) return fallback;
  char* end = nullptr;
  const auto value = std::strtoull(it->second.c_str(), &end, 10);
  return end == it->second.c_str() ? fallback : static_cast<uint64_t>(value);
}

int SystemInfoService::fieldInt(const Fields& fields, const std::string& key, int fallback) {
  const auto it = fields.find(key);
  if (it == fields.end() || it->second.empty()) return fallback;
  char* end = nullptr;
  const auto value = std::strtol(it->second.c_str(), &end, 10);
  return end == it->second.c_str() ? fallback : static_cast<int>(value);
}

double SystemInfoService::fieldDouble(const Fields& fields, const std::string& key, double fallback) {
  const auto it = fields.find(key);
  if (it == fields.end() || it->second.empty()) return fallback;
  char* end = nullptr;
  const auto value = std::strtod(it->second.c_str(), &end);
  return end == it->second.c_str() ? fallback : value;
}

int64_t SystemInfoService::nowMs() {
  return static_cast<int64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace audio_studio::framework::system_info
