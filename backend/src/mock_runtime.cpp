#include "audio_studio.hpp"
#include <iomanip>
#include <iostream>

namespace audiostudio {

namespace {
void printIntegrationTodo(const std::string& callback, const std::string& request_json, const std::string& todo) {
  std::cout << "\n[AudioStudio Backend TODO] " << callback << "\n"
            << "  What to implement: " << todo << "\n"
            << "  Request JSON: " << request_json.substr(0, 1200)
            << (request_json.size() > 1200 ? "...(truncated)" : "") << "\n"
            << std::flush;
}
} // namespace

MockRuntimeEngine::MockRuntimeEngine() {
  rng_.seed(static_cast<unsigned>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
}

double MockRuntimeEngine::rnd(double min, double max) {
  std::lock_guard<std::mutex> lk(rng_mutex_);
  std::uniform_real_distribution<double> dist(min, max);
  return dist(rng_);
}

int MockRuntimeEngine::rndi(int min, int max) {
  std::lock_guard<std::mutex> lk(rng_mutex_);
  std::uniform_int_distribution<int> dist(min, max);
  return dist(rng_);
}

std::string MockRuntimeEngine::validatePipeline(const std::string& pipeline_json) {
  printIntegrationTodo(
      "IRuntimeEngine::validatePipeline",
      pipeline_json,
      "Check graph validity: module existence, port format/channel compatibility, one-output-to-one-input policy, static/runtime parameter policy, and target DSP resource budget.");
  const bool has_nodes = pipeline_json.find("nodes") != std::string::npos;
  const bool has_edges = pipeline_json.find("edges") != std::string::npos;
  std::ostringstream os;
  os << "{\"ok\":" << (has_nodes ? "true" : "false") << ",\"warnings\":[";
  if (!has_edges) os << "\"No edge section found\"";
  os << "],\"errors\":[";
  if (!has_nodes) os << "\"No node section found\"";
  os << "]}";
  return os.str();
}

std::string MockRuntimeEngine::buildPipeline(const std::string& pipeline_json) {
  printIntegrationTodo(
      "IRuntimeEngine::buildPipeline",
      pipeline_json,
      "Compile/build the pipeline for the selected DSP target: allocate buffers, map nodes to DSP cores, generate command/config blobs, and create a runtime session.");
  std::ostringstream os;
  os << "{\"ok\":true,\"session_id\":\"sess_" << rndi(1000, 9999) << "\",\"message\":\"Mock DSP runtime configured\",\"core_map\":{}}";
  return os.str();
}

std::string MockRuntimeEngine::run(const std::string& session_id) {
  printIntegrationTodo(
      "IRuntimeEngine::run",
      session_id,
      "Start DSP runtime/session, enable DMA/audio I/O, start graph scheduler, and begin telemetry collection.");
  running_.store(true);
  return "{\"ok\":true,\"running\":true}";
}

std::string MockRuntimeEngine::stop(const std::string& session_id) {
  printIntegrationTodo(
      "IRuntimeEngine::stop",
      session_id,
      "Stop DSP runtime/session safely, drain/flush buffers, disable audio I/O, and release runtime resources that are not needed while editing.");
  running_.store(false);
  return "{\"ok\":true,\"running\":false}";
}

std::string MockRuntimeEngine::pipelineEditEvent(const std::string& request_json) {
  printIntegrationTodo(
      "IRuntimeEngine::pipelineEditEvent",
      request_json,
      "Handle graph edit events such as node_added, node_removed, connection_added, connection_removed, and pipeline_canvas_ready. Real code should update the project graph model and invalidate/rebuild DSP resources.");
  return "{\"ok\":true,\"callback\":\"IRuntimeEngine::pipelineEditEvent\",\"message\":\"mock edit event accepted\"}";
}

std::string MockRuntimeEngine::pipelineToolAction(const std::string& request_json) {
  printIntegrationTodo(
      "IRuntimeEngine::pipelineToolAction",
      request_json,
      "Handle canvas tool actions. Select/pan/fit are frontend-only today; auto-arrange/delete can be used by backend policy, project graph management, or DSP-control integration.");
  return "{\"ok\":true,\"callback\":\"IRuntimeEngine::pipelineToolAction\",\"message\":\"mock tool action accepted\"}";
}

std::string MockRuntimeEngine::telemetry(const std::vector<std::string>& node_ids) {
  const bool run = running_.load();
  std::ostringstream os;
  os << std::fixed << std::setprecision(2);
  os << "{\"timestamp\":" << static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
  os << ",\"nodeCost\":{";
  for (size_t i = 0; i < node_ids.size(); ++i) {
    if (i) os << ',';
    const double cpu = run ? rnd(0.2, 6.8) : rnd(0.0, 0.12);
    os << "\"" << jsonEscape(node_ids[i]) << "\":{";
    os << "\"cpu\":" << cpu << ",\"memKb\":" << rndi(48, 980) << ",\"latencyMs\":" << (run ? rnd(0.03, 2.4) : 0.0);
    os << ",\"core\":" << rndi(0, 3) << ",\"rms\":" << rnd(-42, -10) << ",\"peak\":" << rnd(-14, -1) << "}";
  }
  os << "},\"cores\":[";
  for (int i = 0; i < 4; ++i) {
    if (i) os << ',';
    os << "{\"id\":" << i << ",\"load\":" << (run ? rnd(15, 78) : rnd(0, 4));
    os << ",\"temperature\":" << (run ? rnd(42, 65) : rnd(36, 40));
    os << ",\"powerMw\":" << (run ? rndi(450, 1600) : rndi(60, 180)) << "}";
  }
  os << "],\"health\":{";
  os << "\"latencyMs\":" << (run ? rnd(8, 24) : 0) << ",\"bufferOccupancy\":" << (run ? rnd(24, 68) : 0);
  os << ",\"throughput\":" << (run ? rnd(88, 112) : 0) << ",\"xruns\":0,\"memoryMb\":" << rnd(110, 420);
  os << ",\"powerW\":" << (run ? rnd(2.0, 5.8) : rnd(0.2, 0.6)) << "}}";
  return os.str();
}

std::string MockRuntimeEngine::onNodeAction(const std::string& request_json) {
  printIntegrationTodo(
      "INodeController::onNodeAction",
      request_json,
      "Handle node-level actions such as select, inspect, enable/disable, reset, bypass, or future algorithm-specific control callbacks.");
  std::ostringstream os;
  os << "{\"ok\":true,\"callback\":\"INodeController::onNodeAction\",\"mock_value\":" << rnd(0, 1) << "}";
  return os.str();
}

std::string MockRuntimeEngine::updateParameter(const std::string& request_json) {
  printIntegrationTodo(
      "IParameterController::updateParameter",
      request_json,
      "Apply runtime parameter updates to a live DSP node. Static parameters should be rejected or deferred while running; dynamic parameters should map to the real algorithm control API.");
  return "{\"ok\":true,\"callback\":\"IParameterController::updateParameter\",\"apply\":\"mock_next_frame\"}";
}

} // namespace audiostudio
