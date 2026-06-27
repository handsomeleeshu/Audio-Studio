#include "audio_studio.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <utility>

#include "autoconfig.h"

#if defined(CONFIG_RPC_CLIENT) && defined(CONFIG_RPC_TRANSPORT_SOCKET) && defined(CONFIG_DRIVER_SOCKET)
#include "driver_manager.hpp"
#include "json_rpc.hpp"
#include "rpc_socket_transport.hpp"
#define AUDIO_STUDIO_GUI_SYSTEM_INFO_RPC 1
#endif

namespace audiostudio {
namespace {

long long nowMs() {
  return static_cast<long long>(
    std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string queryValue(const std::map<std::string, std::string>& query,
                       const std::string& key,
                       const std::string& fallback = {}) {
  const auto it = query.find(key);
  return it == query.end() ? fallback : it->second;
}

#if defined(AUDIO_STUDIO_GUI_SYSTEM_INFO_RPC)
const audio_studio::rpc::JsonValue* field(const audio_studio::rpc::JsonValue& value,
                                           const std::string& name) {
  if (!value.isObject() || !value.has(name)) return nullptr;
  return &value.at(name);
}

std::string stringField(const audio_studio::rpc::JsonValue& value,
                        const std::string& name,
                        const std::string& fallback = {}) {
  const auto* item = field(value, name);
  return item && item->isString() ? item->asString() : fallback;
}

double numberField(const audio_studio::rpc::JsonValue& value,
                   const std::string& name,
                   double fallback = 0.0) {
  const auto* item = field(value, name);
  return item && item->isNumber() ? item->asDouble() : fallback;
}

uint64_t u64Field(const audio_studio::rpc::JsonValue& value,
                  const std::string& name,
                  uint64_t fallback = 0) {
  const auto* item = field(value, name);
  return item && item->isNumber() ? item->asUInt64() : fallback;
}

bool boolField(const audio_studio::rpc::JsonValue& value,
               const std::string& name,
               bool fallback = false) {
  const auto* item = field(value, name);
  return item && item->isBool() ? item->asBool() : fallback;
}

audio_studio::rpc::JsonValue callSystemInfo(const std::string& host, uint16_t port, const std::string& method) {
  auto& drivers = audio_studio::drivers::DriverManager::instance();
  audio_studio::drivers::DriverManagerConfig driver_config;
  driver_config.enable_os = false;
  driver_config.enable_socket = true;
  driver_config.enable_filesystem = false;
  driver_config.enable_pipe = false;
  driver_config.enable_dynlib = false;
  driver_config.enable_datalink = false;
  driver_config.enable_audio = false;
  driver_config.enable_control = false;
  driver_config.enable_log = false;
  driver_config.enable_dump = false;
  auto status = drivers.initialize(driver_config);
  if (!status.ok()) throw std::runtime_error(status.message());

  audio_studio::rpc::SocketRpcEndpoint endpoint;
  endpoint.host = host;
  endpoint.port = port;
  endpoint.timeout_ms = 1000;
  audio_studio::rpc::SocketJsonRpcTransport transport(drivers.socket(), endpoint);
  audio_studio::rpc::JsonRpcClient client(transport);
  return client.call(method);
}
#endif

std::string disconnectedFrame(const std::string& mode, const std::string& error = {}) {
  std::ostringstream os;
  os << "{\"ok\":true,\"mode\":\"" << mode << "\",\"connected\":false,\"running\":false"
     << ",\"timestamp_ms\":" << nowMs();
  if (mode == "system_info_algorithm_cost") {
    os << ",\"costs\":[]";
  } else if (mode == "system_info_dsp_core_loading") {
    os << ",\"core_count\":0,\"cores\":[],\"summary\":{\"totalLoad\":0,\"totalLoadPercent\":0,\"headroom\":0,\"headroomPercent\":0}";
  } else if (mode == "system_info_health") {
    os << ",\"summary\":{\"connected\":false,\"heapRows\":0}"
       << ",\"rows\":[{\"label\":\"Audio Studio Info Heartbeat\",\"value\":\"Disconnected\",\"percent\":0,\"severity\":\"idle\"}]";
  }
  if (!error.empty()) os << ",\"error\":\"" << jsonEscape(error) << "\"";
  os << "}";
  return os.str();
}

std::string heapValue(uint64_t free_count, uint64_t total_count, uint64_t used_bytes, uint64_t free_bytes) {
  std::ostringstream os;
  if (total_count > 0) {
    os << free_count << " free / " << total_count << " blocks";
    return os.str();
  }
  if (used_bytes || free_bytes) {
    os << (used_bytes / 1024) << " KB used / " << (free_bytes / 1024) << " KB free";
    return os.str();
  }
  return "reported";
}

} // namespace

RpcAlgorithmCostController::RpcAlgorithmCostController(std::string host, uint16_t port)
  : host_(std::move(host)), port_(port) {}

std::string RpcAlgorithmCostController::liveCosts(const std::map<std::string, std::string>& query) {
#if defined(AUDIO_STUDIO_GUI_SYSTEM_INFO_RPC)
  try {
    const auto snapshot = callSystemInfo(host_, port_, "systemInfo.snapshot");
    const bool connected = boolField(snapshot, "connected", false);
    const auto requested_nodes = splitCsv(queryValue(query, "nodes"));
    const auto* components_value = field(snapshot, "components");
    const auto* components = components_value && components_value->isArray()
                               ? &components_value->asArray()
                               : nullptr;

    std::ostringstream os;
    os << std::fixed << std::setprecision(3);
    os << "{\"ok\":true,\"mode\":\"system_info_algorithm_cost\",\"connected\":" << (connected ? "true" : "false")
       << ",\"running\":" << (connected ? "true" : "false")
       << ",\"timestamp_ms\":" << u64Field(snapshot, "timestamp_ms", static_cast<uint64_t>(nowMs()))
       << ",\"costs\":[";
    const size_t component_count = components ? components->size() : 0;
    const size_t count = requested_nodes.empty() ? component_count : requested_nodes.size();
    for (size_t i = 0; i < count; ++i) {
      const auto& component = component_count == 0 ? snapshot : (*components)[std::min(i, component_count - 1)];
      if (i) os << ',';
      const std::string node_id = requested_nodes.empty()
                                  ? stringField(component, "node_id", "component_" + std::to_string(i))
                                  : requested_nodes[i];
      os << "{\"node_id\":\"" << jsonEscape(node_id) << "\""
         << ",\"cpu\":" << numberField(component, "cpu_percent", 0.0)
         << ",\"core\":" << static_cast<int>(numberField(component, "core", 0.0))
         << ",\"mem_kb\":" << static_cast<uint64_t>((u64Field(component, "memory_bytes", 0) + 1023) / 1024)
         << ",\"latency_ms\":" << numberField(component, "latency_ms", 0.0)
         << "}";
    }
    os << "]}";
    return os.str();
  } catch (const std::exception& error) {
    return disconnectedFrame("system_info_algorithm_cost", error.what());
  }
#else
  (void)query;
  return disconnectedFrame("system_info_algorithm_cost", "GUI backend was built without RPC client support");
#endif
}

RpcDspCoreLoadingController::RpcDspCoreLoadingController(std::string host, uint16_t port)
  : host_(std::move(host)), port_(port) {}

std::string RpcDspCoreLoadingController::liveCoreLoading(const std::map<std::string, std::string>& query) {
#if defined(AUDIO_STUDIO_GUI_SYSTEM_INFO_RPC)
  try {
    const auto snapshot = callSystemInfo(host_, port_, "systemInfo.snapshot");
    const bool connected = boolField(snapshot, "connected", false);
    const int requested = std::max(1, std::min(64, std::stoi(queryValue(query, "cores", "4"))));
    const auto* cores_value = field(snapshot, "cores");
    const auto* cores = cores_value && cores_value->isArray()
                          ? &cores_value->asArray()
                          : nullptr;

    double sum = 0.0;
    std::ostringstream os;
    os << std::fixed << std::setprecision(2);
    os << "{\"ok\":true,\"mode\":\"system_info_dsp_core_loading\",\"connected\":" << (connected ? "true" : "false")
       << ",\"running\":" << (connected ? "true" : "false")
       << ",\"timestamp_ms\":" << u64Field(snapshot, "timestamp_ms", static_cast<uint64_t>(nowMs()))
       << ",\"core_count\":" << ((cores == nullptr || cores->empty()) ? requested : cores->size())
       << ",\"cores\":[";
    const size_t count = (cores == nullptr || cores->empty()) ? static_cast<size_t>(requested) : cores->size();
    for (size_t i = 0; i < count; ++i) {
      const auto& core = (cores == nullptr || cores->empty()) ? snapshot : (*cores)[i];
      const double load = numberField(core, "load_percent", 0.0);
      sum += load;
      if (i) os << ',';
      os << "{\"id\":" << static_cast<int>(numberField(core, "id", static_cast<double>(i)))
         << ",\"load\":" << load
         << ",\"loadPercent\":" << load
         << ",\"temperature\":0.0,\"temperatureC\":0.0"
         << ",\"powerW\":0.0,\"powerMw\":0}";
    }
    const double total = count ? sum / static_cast<double>(count) : 0.0;
    const double headroom = std::max(0.0, 100.0 - total);
    os << "],\"summary\":{\"totalLoad\":" << total
       << ",\"totalLoadPercent\":" << total
       << ",\"headroom\":" << headroom
       << ",\"headroomPercent\":" << headroom << "}}";
    return os.str();
  } catch (const std::exception& error) {
    return disconnectedFrame("system_info_dsp_core_loading", error.what());
  }
#else
  (void)query;
  return disconnectedFrame("system_info_dsp_core_loading", "GUI backend was built without RPC client support");
#endif
}

RpcSystemHealthController::RpcSystemHealthController(std::string host, uint16_t port)
  : host_(std::move(host)), port_(port) {}

std::string RpcSystemHealthController::liveHealth(const std::map<std::string, std::string>& query) {
  (void)query;
#if defined(AUDIO_STUDIO_GUI_SYSTEM_INFO_RPC)
  try {
    const auto health = callSystemInfo(host_, port_, "systemInfo.health");
    const bool connected = boolField(health, "connected", false);
    const auto* rows_value = field(health, "rows");
    const auto* rows = rows_value && rows_value->isArray()
                         ? &rows_value->asArray()
                         : nullptr;

    std::ostringstream os;
    os << std::fixed << std::setprecision(2);
    os << "{\"ok\":true,\"mode\":\"system_info_health\",\"connected\":" << (connected ? "true" : "false")
       << ",\"running\":" << (connected ? "true" : "false")
       << ",\"timestamp_ms\":" << u64Field(health, "timestamp_ms", static_cast<uint64_t>(nowMs()))
       << ",\"summary\":{\"connected\":" << (connected ? "true" : "false")
       << ",\"heapRows\":" << (rows ? rows->size() : 0) << "}"
       << ",\"rows\":[";
    const size_t row_count = rows ? rows->size() : 0;
    for (size_t i = 0; i < row_count; ++i) {
      const auto& row = (*rows)[i];
      const uint64_t free_count = u64Field(row, "free_count", 0);
      const uint64_t total_count = u64Field(row, "total_count", 0);
      const uint64_t used_bytes = u64Field(row, "used_bytes", 0);
      const uint64_t free_bytes = u64Field(row, "free_bytes", 0);
      double percent = 0.0;
      if (total_count > 0) percent = 100.0 * static_cast<double>(total_count - free_count) / static_cast<double>(total_count);
      else if (used_bytes + free_bytes > 0) percent = 100.0 * static_cast<double>(used_bytes) / static_cast<double>(used_bytes + free_bytes);
      if (i) os << ',';
      const std::string status = stringField(row, "status", "ok");
      os << "{\"label\":\"" << jsonEscape(stringField(row, "name", stringField(row, "id", "System Info"))) << "\""
         << ",\"value\":\"" << jsonEscape(heapValue(free_count, total_count, used_bytes, free_bytes)) << "\""
         << ",\"percent\":" << std::max(0.0, std::min(100.0, percent))
         << ",\"severity\":\"" << (status == "warning" ? "warn" : status) << "\"}";
    }
    if (row_count == 0) {
      os << "{\"label\":\"Audio Studio Info Heartbeat\",\"value\":\"Disconnected\",\"percent\":0,\"severity\":\"idle\"}";
    }
    os << "]}";
    return os.str();
  } catch (const std::exception& error) {
    return disconnectedFrame("system_info_health", error.what());
  }
#else
  return disconnectedFrame("system_info_health", "GUI backend was built without RPC client support");
#endif
}

} // namespace audiostudio
