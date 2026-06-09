#include "audio_studio.hpp"
#include <atomic>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

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


static std::string simpleJsonField(const std::string& json, const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  auto pos = json.find(needle);
  if (pos == std::string::npos) return {};
  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) return {};
  pos = json.find('"', pos);
  if (pos == std::string::npos) return {};
  auto end = pos + 1;
  bool esc = false;
  for (; end < json.size(); ++end) {
    char c = json[end];
    if (esc) { esc = false; continue; }
    if (c == '\\') { esc = true; continue; }
    if (c == '"') break;
  }
  if (end <= pos + 1 || end >= json.size()) return {};
  return json.substr(pos + 1, end - pos - 1);
}

struct FakeProbePortSpec {
  std::string dir;
  std::string name;
  int channels = 1;
};

static std::vector<std::string> splitSimple(const std::string& s, char delim) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == delim) { out.push_back(cur); cur.clear(); }
    else cur.push_back(c);
  }
  out.push_back(cur);
  return out;
}

static std::vector<FakeProbePortSpec> parseInspectorPorts(const std::string& csv) {
  std::vector<FakeProbePortSpec> ports;
  for (const auto& token : splitSimple(csv, ',')) {
    if (token.empty()) continue;
    auto fields = splitSimple(token, ':');
    FakeProbePortSpec p;
    p.dir = fields.size() > 0 && !fields[0].empty() ? fields[0] : "in";
    p.name = fields.size() > 1 && !fields[1].empty() ? fields[1] : (p.dir == "out" ? "out" : "in");
    try { p.channels = fields.size() > 2 ? std::max(1, std::stoi(fields[2])) : 1; } catch (...) { p.channels = 1; }
    ports.push_back(p);
  }
  if (ports.empty()) {
    ports.push_back({"in", "in", 2});
    ports.push_back({"out", "out", 2});
  }
  return ports;
}

static std::string queryGet(const std::map<std::string, std::string>& q, const std::string& key, const std::string& fallback = {}) {
  auto it = q.find(key);
  return it == q.end() ? fallback : it->second;
}

static bool queryBool(const std::map<std::string, std::string>& q, const std::string& key) {
  const auto v = queryGet(q, key);
  return v == "1" || v == "true" || v == "yes" || v == "running";
}

FakeInspectorController::FakeInspectorController() {
  rng_.seed(static_cast<unsigned>(std::chrono::high_resolution_clock::now().time_since_epoch().count() ^ 0x5a17u));
}

double FakeInspectorController::rnd(double min, double max) {
  std::lock_guard<std::mutex> lk(mutex_);
  std::uniform_real_distribution<double> dist(min, max);
  return dist(rng_);
}

int FakeInspectorController::rndi(int min, int max) {
  std::lock_guard<std::mutex> lk(mutex_);
  std::uniform_int_distribution<int> dist(min, max);
  return dist(rng_);
}

std::string FakeInspectorController::inspectNode(const std::string& request_json) {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    current_node_id_ = simpleJsonField(request_json, "node_id");
    current_node_name_ = simpleJsonField(request_json, "node_name");
    current_module_type_ = simpleJsonField(request_json, "module_type");
    last_request_json_ = request_json;
  }

  printIntegrationTodo("IInspectorController::inspectNode", request_json,
    "bind Inspector backend context to selected node/module/ports; real product can subclass IInspectorController and map node_id to DSP runtime handles.");

  std::ostringstream os;
  os << "{\"ok\":true,\"callback\":\"IInspectorController::inspectNode\""
     << ",\"node_id\":\"" << jsonEscape(simpleJsonField(request_json, "node_id")) << "\""
     << ",\"mode\":\"fake\"}";
  return os.str();
}

std::string FakeInspectorController::liveData(const std::map<std::string, std::string>& query) {
  static std::atomic<int> live_count{0};
  const int count = ++live_count;

  const bool run = queryBool(query, "running");
  std::string node_id = queryGet(query, "node_id");
  std::string node_name = queryGet(query, "node_name");
  {
    std::lock_guard<std::mutex> lk(mutex_);
    if (node_id.empty()) node_id = current_node_id_;
    if (node_name.empty()) node_name = current_node_name_;
  }

  const auto ports = parseInspectorPorts(queryGet(query, "ports"));
  int in_ch = 0, out_ch = 0;
  for (const auto& p : ports) {
    if (p.dir == "out") out_ch += p.channels;
    else in_ch += p.channels;
  }
  if (in_ch <= 0) in_ch = 1;
  if (out_ch <= 0) out_ch = in_ch;

  if (count == 1 || count % 25 == 0) {
    std::cout << "\n[AudioStudio Backend CALLBACK] IInspectorController::liveData"
              << "\n  Fake live Inspector data. request_count=" << count
              << " running=" << (run ? "true" : "false")
              << " node_id=" << node_id
              << " ports=" << ports.size() << "\n" << std::flush;
  }

  const auto now_ms = static_cast<long long>(
    std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());

  std::ostringstream os;
  os << std::fixed << std::setprecision(3);
  os << "{\"ok\":true,\"mode\":\"fake\",\"running\":" << (run ? "true" : "false")
     << ",\"timestamp_ms\":" << now_ms
     << ",\"node_id\":\"" << jsonEscape(node_id) << "\""
     << ",\"node_name\":\"" << jsonEscape(node_name) << "\"";

  os << ",\"general\":{";
  if (run) {
    const double rms = -rnd(12.0, 34.0);
    const double peak = -rnd(1.2, 10.0);
    os << "\"input\":\"" << in_ch << "ch · 48 kHz · 16-bit\","
       << "\"output\":\"" << out_ch << "ch · 48 kHz · 16-bit\","
       << "\"rmsIn\":" << rms << ",\"peakOut\":" << peak
       << ",\"frameSizeSamples\":256,\"frameMs\":5.333";
  } else {
    os << "\"input\":\"N/A\",\"output\":\"N/A\",\"rmsIn\":null,\"peakOut\":null,"
       << "\"frameSizeSamples\":null,\"frameMs\":null";
  }
  os << "}";

  os << ",\"ports\":[";
  const double phase = rnd(0.0, 6.28);
  for (size_t pi = 0; pi < ports.size(); ++pi) {
    const auto& p = ports[pi];
    if (pi) os << ',';
    const std::string key = p.dir + ":" + p.name;
    os << "{\"key\":\"" << jsonEscape(key) << "\","
       << "\"dir\":\"" << jsonEscape(p.dir) << "\","
       << "\"name\":\"" << jsonEscape(p.name) << "\","
       << "\"channels\":" << p.channels << ","
       << "\"sampleRate\":48000,\"bitDepth\":16,";
    if (run) os << "\"rms\":" << -rnd(10.0, 35.0) << ",\"peak\":" << -rnd(0.8, 12.0) << ",";
    else os << "\"rms\":null,\"peak\":null,";

    os << "\"waveform\":[";
    for (int i = 0; i < 96; ++i) {
      if (i) os << ',';
      double v = 0.0;
      if (run) {
        v = std::sin(i * 0.13 + phase + static_cast<double>(pi)) * 0.62
          + std::sin(i * 0.37 + phase * 0.5) * 0.18
          + rnd(-0.06, 0.06);
        if (v > 1.0) v = 1.0;
        if (v < -1.0) v = -1.0;
      }
      os << v;
    }
    os << "],\"spectrum\":[";
    for (int i = 0; i < 64; ++i) {
      if (i) os << ',';
      double v = 0.0;
      if (run) {
        v = (std::sin(i * 0.19 + phase + pi * 0.3) + 1.0) * 0.28 + (1.0 - i / 72.0) * 0.38 + rnd(0.0, 0.10);
        if (v > 1.0) v = 1.0;
        if (v < 0.0) v = 0.0;
      }
      os << v;
    }
    os << "]}";
  }
  os << "]}";
  return os.str();
}


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
