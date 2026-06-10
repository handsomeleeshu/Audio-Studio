#include "audio_studio.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <ctime>
#include <cstdio>

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

static int queryInt(const std::map<std::string, std::string>& q, const std::string& key, int fallback) {
  try {
    const auto v = queryGet(q, key);
    return v.empty() ? fallback : std::stoi(v);
  } catch (...) {
    return fallback;
  }
}

std::string FakeInspectorController::inspectBuffer(const std::string& request_json) {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    current_buffer_key_ = simpleJsonField(request_json, "edge_key");
    current_buffer_from_ = simpleJsonField(request_json, "from");
    current_buffer_to_ = simpleJsonField(request_json, "to");
    last_request_json_ = request_json;
  }

  printIntegrationTodo("IInspectorController::inspectBuffer", request_json,
    "bind Inspector backend context to a runtime buffer between two modules; real product can map edge_key/from/to to a DSP ringbuffer or shared memory handle.");

  std::ostringstream os;
  os << "{\"ok\":true,\"callback\":\"IInspectorController::inspectBuffer\""
     << ",\"edge_key\":\"" << jsonEscape(simpleJsonField(request_json, "edge_key")) << "\""
     << ",\"mode\":\"fake\"}";
  return os.str();
}

std::string FakeInspectorController::bufferLiveData(const std::map<std::string, std::string>& query) {
  static std::atomic<int> buffer_live_count{0};
  static std::atomic<long long> sine_sample_cursor{0};
  const int count = ++buffer_live_count;

  const bool run = queryBool(query, "running");
  const int channels = std::max(1, queryInt(query, "channels", 2));
  const int sample_rate = std::max(8000, queryInt(query, "sample_rate", 48000));
  // The fake dump stream is PCM16. Keep format.bits aligned with pcm16 so the
  // frontend WAV writer can write a correct file without conversion ambiguity.
  const int bits = 16;
  const int frame_samples = std::max(64, std::min(2048, queryInt(query, "frame_samples", 256)));
  std::string edge_key = queryGet(query, "edge_key");
  std::string from = queryGet(query, "from");
  std::string to = queryGet(query, "to");
  {
    std::lock_guard<std::mutex> lk(mutex_);
    if (edge_key.empty()) edge_key = current_buffer_key_;
    if (from.empty()) from = current_buffer_from_;
    if (to.empty()) to = current_buffer_to_;
  }

  if (count == 1 || count % 25 == 0) {
    std::cout << "\n[AudioStudio Backend CALLBACK] IInspectorController::bufferLiveData"
              << "\n  Fake buffer live data: 1kHz sine PCM16. request_count=" << count
              << " running=" << (run ? "true" : "false")
              << " edge_key=" << edge_key
              << " channels=" << channels
              << " sample_rate=" << sample_rate
              << " frame_samples=" << frame_samples << "\n" << std::flush;
  }

  const auto now_ms = static_cast<long long>(
    std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());

  constexpr double kPi = 3.1415926535897932384626433832795;
  constexpr double kToneHz = 1000.0;
  constexpr double kAmp = 0.60;

  const long long start_sample = run ? sine_sample_cursor.fetch_add(frame_samples) : sine_sample_cursor.load();

  std::vector<double> mono(static_cast<size_t>(frame_samples), 0.0);
  std::vector<int> pcm;
  pcm.reserve(static_cast<size_t>(frame_samples * channels));

  double rms_acc = 0.0;
  double peak_abs = 0.0;
  for (int i = 0; i < frame_samples; ++i) {
    double v = 0.0;
    if (run) {
      const double phase = 2.0 * kPi * kToneHz * static_cast<double>(start_sample + i) / static_cast<double>(sample_rate);
      v = std::sin(phase) * kAmp;
    }
    mono[static_cast<size_t>(i)] = v;
    rms_acc += v * v;
    peak_abs = std::max(peak_abs, std::abs(v));
    const int s = static_cast<int>(std::max(-32768.0, std::min(32767.0, std::round(v * 32767.0))));
    for (int ch = 0; ch < channels; ++ch) pcm.push_back(s);
  }

  std::vector<double> wave(96, 0.0);
  for (int i = 0; i < 96; ++i) {
    if (run) {
      const int src_i = static_cast<int>((static_cast<long long>(i) * frame_samples) / 96);
      wave[static_cast<size_t>(i)] = mono[static_cast<size_t>(std::min(frame_samples - 1, src_i))];
    }
  }

  std::vector<double> spec(64, 0.0);
  if (run) {
    // Lightweight fake spectrum with a clear peak around 1 kHz.
    const double bin_hz = 20000.0 / 64.0;
    for (int i = 0; i < 64; ++i) {
      const double hz = (i + 0.5) * bin_hz;
      const double dist = std::abs(hz - kToneHz);
      double v = std::exp(-(dist * dist) / (2.0 * 260.0 * 260.0)) * 0.95 + 0.08 * std::exp(-i / 28.0);
      if (v > 1.0) v = 1.0;
      if (v < 0.0) v = 0.0;
      spec[static_cast<size_t>(i)] = v;
    }
  }

  const double rms = run ? 20.0 * std::log10(std::max(1e-5, std::sqrt(rms_acc / std::max(1, frame_samples)))) : -120.0;
  const double peak = run ? 20.0 * std::log10(std::max(1e-5, peak_abs)) : -120.0;

  std::ostringstream os;
  os << std::fixed << std::setprecision(3);
  os << "{\"ok\":true,\"mode\":\"fake_buffer_1khz\",\"buffer\":true,\"running\":" << (run ? "true" : "false")
     << ",\"timestamp_ms\":" << now_ms
     << ",\"edge_key\":\"" << jsonEscape(edge_key) << "\""
     << ",\"from\":\"" << jsonEscape(from) << "\""
     << ",\"to\":\"" << jsonEscape(to) << "\""
     << ",\"toneHz\":1000"
     << ",\"format\":{\"channels\":" << channels
     << ",\"sampleRate\":" << sample_rate
     << ",\"bits\":" << bits
     << ",\"frameSamples\":" << frame_samples
     << ",\"label\":\"" << channels << "ch · " << (sample_rate / 1000) << " kHz · " << bits << "-bit\"}"
     << ",\"general\":{";

  if (run) {
    os << "\"format\":\"" << channels << "ch · " << (sample_rate / 1000) << " kHz · " << bits << "-bit\","
       << "\"rms\":" << rms << ",\"peak\":" << peak
       << ",\"frameSizeSamples\":" << frame_samples
       << ",\"frameMs\":" << (1000.0 * frame_samples / sample_rate);
  } else {
    os << "\"format\":\"N/A\",\"rms\":null,\"peak\":null,\"frameSizeSamples\":null,\"frameMs\":null";
  }

  os << "},\"ports\":[{\"key\":\"buffer\",\"dir\":\"buffer\",\"name\":\"buffer\",\"channels\":" << channels
     << ",\"sampleRate\":" << sample_rate << ",\"bitDepth\":" << bits
     << ",\"rms\":" << (run ? rms : -120.0)
     << ",\"peak\":" << (run ? peak : -120.0)
     << ",\"waveform\":[";
  for (size_t i = 0; i < wave.size(); ++i) { if (i) os << ','; os << wave[i]; }
  os << "],\"spectrum\":[";
  for (size_t i = 0; i < spec.size(); ++i) { if (i) os << ','; os << spec[i]; }
  os << "]}],\"pcm16\":[";
  for (size_t i = 0; i < pcm.size(); ++i) { if (i) os << ','; os << pcm[i]; }
  os << "]}";
  return os.str();
}


FakeAlgorithmCostController::FakeAlgorithmCostController() {
  rng_.seed(static_cast<unsigned>(std::chrono::high_resolution_clock::now().time_since_epoch().count() ^ 0x48c05u));
}

double FakeAlgorithmCostController::rnd(double min, double max) {
  std::lock_guard<std::mutex> lk(mutex_);
  std::uniform_real_distribution<double> dist(min, max);
  return dist(rng_);
}

int FakeAlgorithmCostController::rndi(int min, int max) {
  std::lock_guard<std::mutex> lk(mutex_);
  std::uniform_int_distribution<int> dist(min, max);
  return dist(rng_);
}

std::string FakeAlgorithmCostController::liveCosts(const std::map<std::string, std::string>& query) {
  static std::atomic<int> cost_count{0};
  const int count = ++cost_count;
  const bool run = queryBool(query, "running");
  const auto node_ids = splitSimple(queryGet(query, "nodes"), ',');
  const auto core_tokens = splitSimple(queryGet(query, "cores"), ',');

  if (count == 1 || count % 10 == 0) {
    std::cout << "\n[AudioStudio Backend CALLBACK] IAlgorithmCostController::liveCosts"
              << "\n  Fake per-algorithm runtime cost. request_count=" << count
              << " running=" << (run ? "true" : "false")
              << " node_count=" << node_ids.size() << "\n" << std::flush;
  }

  const auto now_ms = static_cast<long long>(
    std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());

  std::ostringstream os;
  os << std::fixed << std::setprecision(3);
  os << "{\"ok\":true,\"mode\":\"fake_algorithm_cost\",\"running\":" << (run ? "true" : "false")
     << ",\"timestamp_ms\":" << now_ms << ",\"costs\":[";
  for (size_t i = 0; i < node_ids.size(); ++i) {
    if (i) os << ',';
    int core = 0;
    try { core = i < core_tokens.size() && !core_tokens[i].empty() ? std::stoi(core_tokens[i]) : rndi(0, 3); }
    catch (...) { core = rndi(0, 3); }
    const double cpu = run ? (rnd(0.0, 1.0) > 0.84 ? rnd(62.0, 96.0) : rnd(0.4, 38.0)) : 0.0;
    os << "{\"node_id\":\"" << jsonEscape(node_ids[i]) << "\",\"cpu\":" << cpu
       << ",\"core\":" << core;
    if (run) {
      os << ",\"mem_kb\":" << rndi(48, 896)
         << ",\"latency_ms\":" << rnd(0.035, 3.20);
    } else {
      os << ",\"mem_kb\":null,\"latency_ms\":null";
    }
    os << "}";
  }
  os << "]}";
  return os.str();
}



FakeDspCoreLoadingController::FakeDspCoreLoadingController() {
  rng_.seed(static_cast<unsigned>(std::chrono::high_resolution_clock::now().time_since_epoch().count() ^ 0xd5c065u));
}

double FakeDspCoreLoadingController::rnd(double min, double max) {
  std::lock_guard<std::mutex> lk(mutex_);
  std::uniform_real_distribution<double> dist(min, max);
  return dist(rng_);
}

int FakeDspCoreLoadingController::rndi(int min, int max) {
  std::lock_guard<std::mutex> lk(mutex_);
  std::uniform_int_distribution<int> dist(min, max);
  return dist(rng_);
}

std::string FakeDspCoreLoadingController::liveCoreLoading(const std::map<std::string, std::string>& query) {
  static std::atomic<int> core_loading_count{0};
  const int count = ++core_loading_count;
  const bool run = queryBool(query, "running");
  const int requested = std::max(1, std::min(64, queryInt(query, "cores", 4)));

  if (count == 1 || count % 10 == 0) {
    std::cout << "\n[AudioStudio Backend CALLBACK] IDspCoreLoadingController::liveCoreLoading"
              << "\n  Fake DSP core loading data. request_count=" << count
              << " running=" << (run ? "true" : "false")
              << " core_count=" << requested << "\n" << std::flush;
  }

  const auto now_ms = static_cast<long long>(
    std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());

  std::ostringstream os;
  os << std::fixed << std::setprecision(2);
  os << "{\"ok\":true,\"mode\":\"fake_dsp_core_loading\",\"running\":" << (run ? "true" : "false")
     << ",\"timestamp_ms\":" << now_ms
     << ",\"core_count\":" << requested
     << ",\"cores\":[";

  double sum = 0.0;
  for (int i = 0; i < requested; ++i) {
    if (i) os << ',';
    double load = 0.0;
    double temp = rnd(36.0, 40.5);
    double power = rnd(0.10, 0.24);
    if (run) {
      const double branch = rnd(0.0, 1.0);
      load = branch > 0.88 ? rnd(64.0, 92.0) : rnd(10.0, 58.0);
      if (requested > 4 && i >= 4) load *= rnd(0.35, 0.72);
      temp = 43.0 + load * 0.22 + rnd(-1.2, 1.4);
      power = 0.32 + load / 100.0 * 1.55 + rnd(-0.04, 0.06);
    }
    load = std::max(0.0, std::min(100.0, load));
    sum += load;
    os << "{\"id\":" << i
       << ",\"load\":" << load
       << ",\"loadPercent\":" << load
       << ",\"temperature\":" << temp
       << ",\"temperatureC\":" << temp
       << ",\"powerW\":" << power
       << ",\"powerMw\":" << static_cast<int>(power * 1000.0) << "}";
  }
  const double total = requested > 0 ? sum / requested : 0.0;
  const double headroom = std::max(0.0, 100.0 - total);
  os << "],\"summary\":{\"totalLoad\":" << total
     << ",\"totalLoadPercent\":" << total
     << ",\"headroom\":" << headroom
     << ",\"headroomPercent\":" << headroom << "}}";
  return os.str();
}


static long long dashboardNowMsV69() {
  return static_cast<long long>(
    std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());
}

static std::string dashboardTimeTextV69(long long ms) {
  std::time_t sec = static_cast<std::time_t>(ms / 1000);
  std::tm tmv{};
#if defined(_WIN32)
  localtime_s(&tmv, &sec);
#else
  localtime_r(&sec, &tmv);
#endif
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  return std::string(buf);
}

FakeEventLogController::FakeEventLogController() {
  rng_.seed(static_cast<unsigned>(std::chrono::high_resolution_clock::now().time_since_epoch().count() ^ 0xe709u));
  std::lock_guard<std::mutex> lk(mutex_);
  appendLocked("ok", "Backend event log ready", "EVENT LOG is owned by /api/event-log/live", "backend");
  appendLocked("info", "System Health API ready", "SYSTEM HEALTH uses /api/system/health/live", "backend");
  appendLocked("info", "Audio I/O API ready", "AUDIO I/O uses /api/audio/io/live", "backend");
}

std::int64_t FakeEventLogController::nowMs() const {
  return dashboardNowMsV69();
}

void FakeEventLogController::appendLocked(const std::string& kind, const std::string& message, const std::string& detail, const std::string& source) {
  EventLogEntry e;
  e.id = next_id_++;
  e.timestamp_ms = nowMs();
  e.time = dashboardTimeTextV69(e.timestamp_ms);
  e.kind = kind.empty() ? "info" : kind;
  e.message = message.empty() ? "UI event" : message;
  e.detail = detail;
  e.source = source.empty() ? "frontend" : source;
  events_.push_back(std::move(e));
  if (events_.size() > 240) events_.erase(events_.begin(), events_.begin() + static_cast<long>(events_.size() - 240));
}

std::string FakeEventLogController::postEvent(const std::string& request_json) {
  std::string kind = simpleJsonField(request_json, "kind");
  std::string msg = simpleJsonField(request_json, "msg");
  if (msg.empty()) msg = simpleJsonField(request_json, "message");
  std::string sub = simpleJsonField(request_json, "sub");
  if (sub.empty()) sub = simpleJsonField(request_json, "detail");
  std::string source = simpleJsonField(request_json, "source");
  if (source.empty()) source = "frontend_ui";
  {
    std::lock_guard<std::mutex> lk(mutex_);
    appendLocked(kind, msg, sub, source);
  }
  printIntegrationTodo("IEventLogController::postEvent", request_json,
    "record UI/runtime events in backend-owned event log; real product can forward these to persistent logging or firmware trace buffers.");
  return "{\"ok\":true,\"callback\":\"IEventLogController::postEvent\",\"mode\":\"fake_event_log\"}";
}

std::string FakeEventLogController::liveEvents(const std::map<std::string, std::string>& query) {
  const bool run = queryBool(query, "running");
  const int limit = std::max(1, std::min(200, queryInt(query, "limit", 80)));
  const long long now = nowMs();
  {
    std::lock_guard<std::mutex> lk(mutex_);
    if (run && (last_generated_ms_ == 0 || now - last_generated_ms_ > 1200)) {
      std::uniform_int_distribution<int> dist(0, 5);
      switch (dist(rng_)) {
        case 0: appendLocked("info", "Runtime heartbeat", "DSP scheduler tick and audio I/O are active", "mock_runtime"); break;
        case 1: appendLocked("ok", "Audio frame processed", "256 samples · 48 kHz · no underrun", "mock_runtime"); break;
        case 2: appendLocked("info", "System health sampled", "Latency, buffer and power counters updated", "mock_runtime"); break;
        case 3: appendLocked("info", "Audio I/O meters updated", "Input/output dBFS frame received", "mock_runtime"); break;
        case 4: appendLocked("info", "Telemetry pushed", "Node cost and dashboard data refreshed", "mock_runtime"); break;
        default: appendLocked("warn", "Transient buffer pressure", "Buffer occupancy is high but still below xrun threshold", "mock_runtime"); break;
      }
      last_generated_ms_ = now;
    }
  }

  std::lock_guard<std::mutex> lk(mutex_);
  std::ostringstream os;
  os << "{\"ok\":true,\"mode\":\"fake_event_log\",\"running\":" << (run ? "true" : "false")
     << ",\"timestamp_ms\":" << now << ",\"events\":[";
  const size_t count = std::min(static_cast<size_t>(limit), events_.size());
  for (size_t out = 0; out < count; ++out) {
    const auto& e = events_[events_.size() - 1 - out];
    if (out) os << ',';
    os << "{\"id\":" << e.id
       << ",\"timestamp_ms\":" << e.timestamp_ms
       << ",\"time\":\"" << jsonEscape(e.time) << "\""
       << ",\"kind\":\"" << jsonEscape(e.kind) << "\""
       << ",\"msg\":\"" << jsonEscape(e.message) << "\""
       << ",\"sub\":\"" << jsonEscape(e.detail) << "\""
       << ",\"source\":\"" << jsonEscape(e.source) << "\"}";
  }
  os << "]}";
  return os.str();
}

FakeSystemHealthController::FakeSystemHealthController() {
  rng_.seed(static_cast<unsigned>(std::chrono::high_resolution_clock::now().time_since_epoch().count() ^ 0x51a7u));
}

double FakeSystemHealthController::rnd(double min, double max) {
  std::lock_guard<std::mutex> lk(mutex_);
  std::uniform_real_distribution<double> dist(min, max);
  return dist(rng_);
}

int FakeSystemHealthController::rndi(int min, int max) {
  std::lock_guard<std::mutex> lk(mutex_);
  std::uniform_int_distribution<int> dist(min, max);
  return dist(rng_);
}

std::string FakeSystemHealthController::liveHealth(const std::map<std::string, std::string>& query) {
  const bool run = queryBool(query, "running");
  const int cores = std::max(1, std::min(64, queryInt(query, "cores", 4)));
  const int nodes = std::max(0, queryInt(query, "nodes", 0));
  const long long now = dashboardNowMsV69();

  std::ostringstream os;
  os << std::fixed << std::setprecision(2);
  os << "{\"ok\":true,\"mode\":\"fake_system_health\",\"running\":" << (run ? "true" : "false")
     << ",\"timestamp_ms\":" << now;

  auto row = [&](bool comma, const std::string& label, const std::string& value, double percent, const std::string& severity) {
    if (comma) os << ',';
    os << "{\"label\":\"" << jsonEscape(label) << "\",\"value\":\"" << jsonEscape(value)
       << "\",\"percent\":" << std::max(0.0, std::min(100.0, percent))
       << ",\"severity\":\"" << jsonEscape(severity) << "\"}";
  };

  if (run) {
    const double latency = rnd(13.5, 24.0) + nodes * 0.015;
    const double buffer = rnd(30.0, 58.0);
    const double throughput = rnd(91.0, 112.0);
    const double mem = rnd(250.0, 390.0) + nodes * 2.2;
    const double power = rnd(3.05, 4.95) + cores * 0.02;
    const int xruns = rnd(0.0, 1.0) > 0.985 ? 1 : 0;
    os << ",\"summary\":{\"latencyMs\":" << latency
       << ",\"bufferOccupancy\":" << buffer
       << ",\"memoryMb\":" << mem
       << ",\"throughput\":" << throughput
       << ",\"powerW\":" << power
       << ",\"xruns\":\"" << xruns << "\""
       << ",\"activeCores\":" << cores << "}";
    os << ",\"rows\":[";
    row(false, "End-to-End Latency", std::to_string(latency).substr(0, 4) + " ms", latency / 30.0 * 100.0, latency > 23.0 ? "warn" : "ok");
    row(true, "Buffer Occupancy", std::to_string(static_cast<int>(std::round(buffer))) + "%", buffer, buffer > 70.0 ? "warn" : "ok");
    row(true, "Throughput", std::to_string(throughput).substr(0, 5) + " x realtime", std::min(100.0, throughput), throughput < 100.0 ? "warn" : "ok");
    row(true, "XRuns / Dropouts", std::to_string(xruns), xruns ? 100.0 : 4.0, xruns ? "warn" : "ok");
    row(true, "Memory Usage", std::to_string(static_cast<int>(std::round(mem))) + " MB / 512 MB", mem / 512.0 * 100.0, mem > 460.0 ? "warn" : "ok");
    row(true, "Power Usage", std::to_string(power).substr(0, 4) + " W / 6.50 W", power / 6.5 * 100.0, power > 5.5 ? "warn" : "ok");
    row(true, "Active Cores", std::to_string(cores) + " / " + std::to_string(cores), 100.0, "ok");
    os << "]}";
  } else {
    os << ",\"summary\":{\"latencyMs\":null,\"bufferOccupancy\":null,\"memoryMb\":null,\"throughput\":null,\"powerW\":null,\"xruns\":\"0\",\"activeCores\":0}";
    os << ",\"rows\":[";
    row(false, "End-to-End Latency", "N/A", 0.0, "idle");
    row(true, "Buffer Occupancy", "N/A", 0.0, "idle");
    row(true, "Throughput", "N/A", 0.0, "idle");
    row(true, "XRuns / Dropouts", "0", 0.0, "idle");
    row(true, "Memory Usage", "N/A", 0.0, "idle");
    row(true, "Power Usage", "N/A", 0.0, "idle");
    row(true, "Active Cores", "0 / " + std::to_string(cores), 0.0, "idle");
    os << "]}";
  }
  return os.str();
}

FakeAudioIoController::FakeAudioIoController() {
  rng_.seed(static_cast<unsigned>(std::chrono::high_resolution_clock::now().time_since_epoch().count() ^ 0xa10u));
}

double FakeAudioIoController::rnd(double min, double max) {
  std::lock_guard<std::mutex> lk(mutex_);
  std::uniform_real_distribution<double> dist(min, max);
  return dist(rng_);
}

int FakeAudioIoController::rndi(int min, int max) {
  std::lock_guard<std::mutex> lk(mutex_);
  std::uniform_int_distribution<int> dist(min, max);
  return dist(rng_);
}

std::string FakeAudioIoController::liveAudioIo(const std::map<std::string, std::string>& query) {
  const bool run = queryBool(query, "running");
  const int sample_rate = std::max(8000, queryInt(query, "sample_rate", 48000));
  const long long now = dashboardNowMsV69();
  std::ostringstream os;
  os << std::fixed << std::setprecision(2);
  os << "{\"ok\":true,\"mode\":\"fake_audio_io\",\"running\":" << (run ? "true" : "false")
     << ",\"timestamp_ms\":" << now
     << ",\"format\":{\"sampleRate\":" << sample_rate << ",\"bits\":16,\"label\":\"" << (sample_rate / 1000) << " kHz · 16-bit\"}"
     << ",\"channels\":[";
  const char* ids[4] = {"inL", "inR", "outL", "outR"};
  const char* labels[4] = {"IN L", "IN R", "OUT L", "OUT R"};
  for (int i = 0; i < 4; ++i) {
    if (i) os << ',';
    if (run) {
      const double db = (i < 2) ? -rnd(10.0, 30.0) : -rnd(6.0, 22.0);
      const double height = std::max(5.0, std::min(100.0, (60.0 + db) / 60.0 * 100.0));
      os << "{\"id\":\"" << ids[i] << "\",\"label\":\"" << labels[i] << "\",\"dbfs\":" << db << ",\"height\":" << height << "}";
    } else {
      os << "{\"id\":\"" << ids[i] << "\",\"label\":\"" << labels[i] << "\",\"dbfs\":null,\"height\":0}";
    }
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
