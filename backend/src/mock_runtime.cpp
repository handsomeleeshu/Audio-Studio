#include "audio_studio.hpp"
#include <atomic>
#include <iomanip>
#include <iostream>

namespace audiostudio {

namespace {
void printIntegrationTodo(const std::string& callback, const std::string& request_json, const std::string& todo) {
  std::cout << "\n[AudioStudio Backend CALLBACK] " << callback << "\n"
            << "  TODO for real DSP integration: " << todo << "\n"
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
  printIntegrationTodo("IRuntimeEngine::validatePipeline", pipeline_json,
    "validate graph topology, one-output-to-one-input policy, port/channel/sample-rate compatibility, static/runtime parameter policy and DSP budget.");
  const bool has_nodes = pipeline_json.find("nodes") != std::string::npos;
  std::ostringstream os;
  os << "{\"ok\":" << (has_nodes ? "true" : "false")
     << ",\"warnings\":[],\"errors\":";
  if (has_nodes) os << "[]";
  else os << "[\"No node section found\"]";
  os << "}";
  return os.str();
}

std::string MockRuntimeEngine::buildPipeline(const std::string& pipeline_json) {
  printIntegrationTodo("IRuntimeEngine::buildPipeline", pipeline_json,
    "compile/build DSP graph, allocate intermediate buffers, map nodes to DSP cores, generate command/config blobs and create runtime session.");
  std::ostringstream os;
  os << "{\"ok\":true,\"session_id\":\"sess_" << rndi(1000, 9999)
     << "\",\"message\":\"Mock DSP runtime configured by backend\",\"core_map\":{}}";
  return os.str();
}

std::string MockRuntimeEngine::run(const std::string& session_id) {
  printIntegrationTodo("IRuntimeEngine::run", session_id,
    "start DSP runtime/session, enable audio I/O, DMA, scheduler and telemetry.");
  running_.store(true);
  return "{\"ok\":true,\"running\":true}";
}

std::string MockRuntimeEngine::stop(const std::string& session_id) {
  printIntegrationTodo("IRuntimeEngine::stop", session_id,
    "stop DSP runtime safely, drain buffers, disable audio I/O and unlock pipeline editing.");
  running_.store(false);
  return "{\"ok\":true,\"running\":false}";
}

std::string MockRuntimeEngine::pipelineEditEvent(const std::string& request_json) {
  printIntegrationTodo("IRuntimeEngine::pipelineEditEvent", request_json,
    "update backend project graph for node add/delete/move, connection add/delete, auto-layout, audio-file changes or save.");
  return "{\"ok\":true,\"callback\":\"IRuntimeEngine::pipelineEditEvent\",\"message\":\"mock edit event accepted\"}";
}

std::string MockRuntimeEngine::pipelineToolAction(const std::string& request_json) {
  printIntegrationTodo("IRuntimeEngine::pipelineToolAction", request_json,
    "observe or enforce canvas tool policy such as delete-selected, auto-arrange, pan/select/zoom or future DSP-specific editing operations.");
  return "{\"ok\":true,\"callback\":\"IRuntimeEngine::pipelineToolAction\",\"message\":\"mock tool action accepted\"}";
}

std::string MockRuntimeEngine::telemetry(const std::vector<std::string>& node_ids) {
  static std::atomic<int> telemetry_count{0};
  const int count = ++telemetry_count;
  if (count == 1 || count % 10 == 0) {
    std::cout << "\n[AudioStudio Backend CALLBACK] IRuntimeEngine::telemetry"
              << "\n  Backend is generating fake realtime telemetry. request_count=" << count
              << " node_count=" << node_ids.size() << "\n" << std::flush;
  }

  const bool run = running_.load();
  std::ostringstream os;
  os << std::fixed << std::setprecision(2);
  os << "{\"timestamp\":" << static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
  os << ",\"nodeCost\":{";
  for (size_t i = 0; i < node_ids.size(); ++i) {
    if (i) os << ',';
    const double cpu = run ? rnd(0.2, 8.8) : rnd(0.0, 0.18);
    os << "\"" << jsonEscape(node_ids[i]) << "\":{";
    os << "\"cpu\":" << cpu << ",\"memKb\":" << rndi(64, 720)
       << ",\"latencyMs\":" << (run ? rnd(0.04, 2.8) : 0.0)
       << ",\"core\":" << rndi(0, 3)
       << ",\"rms\":" << rnd(-35, -8)
       << ",\"peak\":" << rnd(-12, -0.4) << "}";
  }
  os << "},\"cores\":[";
  for (int i = 0; i < 4; ++i) {
    if (i) os << ',';
    os << "{\"id\":" << i
       << ",\"load\":" << (run ? rnd(12, 86) : rnd(0, 5))
       << ",\"temperature\":" << (run ? rnd(42, 66) : rnd(35, 40))
       << ",\"powerMw\":" << (run ? rndi(420, 1700) : rndi(60, 180)) << "}";
  }
  os << "],\"health\":{";
  os << "\"latencyMs\":" << (run ? rnd(14, 23) : 0)
     << ",\"bufferOccupancy\":" << (run ? rnd(30, 58) : 0)
     << ",\"throughput\":" << (run ? rnd(88, 108) : 0)
     << ",\"xruns\":" << (run && rnd(0, 1) > 0.97 ? 1 : 0)
     << ",\"memoryMb\":" << rnd(260, 365)
     << ",\"powerW\":" << (run ? rnd(3.1, 4.8) : rnd(0.25, 0.55)) << "}";
  os << ",\"meters\":{";
  os << "\"inL\":" << (run ? -rnd(8, 28) : -60)
     << ",\"inR\":" << (run ? -rnd(8, 28) : -60)
     << ",\"outL\":" << (run ? -rnd(4, 18) : -60)
     << ",\"outR\":" << (run ? -rnd(4, 18) : -60) << "}}";
  return os.str();
}

std::string MockRuntimeEngine::onNodeAction(const std::string& request_json) {
  printIntegrationTodo("INodeController::onNodeAction", request_json,
    "handle node select/inspect/bypass/reset or algorithm-specific control callback.");
  std::ostringstream os;
  os << "{\"ok\":true,\"callback\":\"INodeController::onNodeAction\",\"mock_value\":" << rnd(0, 1) << "}";
  return os.str();
}

std::string MockRuntimeEngine::updateParameter(const std::string& request_json) {
  printIntegrationTodo("IParameterController::updateParameter", request_json,
    "apply runtime parameter update to live DSP algorithm control API; reject/defer static params while running.");
  return "{\"ok\":true,\"callback\":\"IParameterController::updateParameter\",\"apply\":\"mock_next_frame\"}";
}

} // namespace audiostudio
