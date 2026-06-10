#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace audiostudio {

struct HttpRequest {
  std::string method;
  std::string path;
  std::string query;
  std::map<std::string, std::string> headers;
  std::string body;
};

struct HttpResponse {
  int status = 200;
  std::string content_type = "application/json";
  std::string body;
};

struct TelemetryNode {
  std::string node_id;
  double cpu = 0;
  int mem_kb = 0;
  double latency_ms = 0;
  int core = 0;
  double rms = -60;
  double peak = -60;
};

struct TelemetryCore {
  int id = 0;
  double load = 0;
  double temperature = 0;
  int power_mw = 0;
};

class IRuntimeEngine {
public:
  virtual ~IRuntimeEngine() = default;
  virtual std::string validatePipeline(const std::string& pipeline_json) = 0;
  virtual std::string buildPipeline(const std::string& pipeline_json) = 0;
  virtual std::string run(const std::string& session_id) = 0;
  virtual std::string stop(const std::string& session_id) = 0;
  virtual std::string telemetry(const std::vector<std::string>& node_ids) = 0;
  virtual std::string pipelineEditEvent(const std::string& request_json) = 0;
  virtual std::string pipelineToolAction(const std::string& request_json) = 0;
};

class INodeController {
public:
  virtual ~INodeController() = default;
  virtual std::string onNodeAction(const std::string& request_json) = 0;
};

class IParameterController {
public:
  virtual ~IParameterController() = default;
  virtual std::string updateParameter(const std::string& request_json) = 0;
};



struct TargetConfigSnapshot {
  std::string project_file = "A2.json";
  std::string dsp_target = "HiFi5s";
  int cores = 4;
  int dsp_frequency_mhz = 600;
  int sample_rate = 48000;
  int frame_size = 256;
};

class ITargetConfigController {
public:
  virtual ~ITargetConfigController() = default;
  virtual std::string targetConfig(const std::map<std::string, std::string>& query) = 0;
  virtual std::string updateTargetConfig(const std::string& request_json) = 0;
};

class FakeTargetConfigController final : public ITargetConfigController {
public:
  FakeTargetConfigController();
  std::string targetConfig(const std::map<std::string, std::string>& query) override;
  std::string updateTargetConfig(const std::string& request_json) override;
private:
  std::string targetJsonLocked(const std::string& mode) const;
  std::string project_file_ = "A2.json";
  std::string dsp_target_ = "HiFi5s";
  int cores_ = 4;
  int dsp_frequency_mhz_ = 600;
  int sample_rate_ = 48000;
  int frame_size_ = 256;
  int revision_ = 1;
  std::mutex mutex_;
};

struct InspectorPortInfo {
  std::string direction;  // "in" or "out"
  std::string name;
  int channels = 1;
};

struct InspectorNodeInfo {
  std::string node_id;
  std::string node_name;
  std::string module_type;
  std::string pipeline_id;
  std::vector<InspectorPortInfo> ports;
};

struct InspectorLiveFrame {
  bool running = false;
  std::string node_id;
  std::int64_t timestamp_ms = 0;
  double rms_in_dbfs = -120.0;
  double peak_out_dbfs = -120.0;
  int frame_size_samples = 256;
  double frame_ms = 5.33;
};

class IInspectorController {
public:
  virtual ~IInspectorController() = default;
  virtual std::string inspectNode(const std::string& request_json) = 0;
  virtual std::string liveData(const std::map<std::string, std::string>& query) = 0;
  // Buffer Inspector API. A connection line represents a runtime audio buffer.
  virtual std::string inspectBuffer(const std::string& request_json) = 0;
  virtual std::string bufferLiveData(const std::map<std::string, std::string>& query) = 0;
};

class FakeInspectorController final : public IInspectorController {
public:
  FakeInspectorController();
  std::string inspectNode(const std::string& request_json) override;
  std::string liveData(const std::map<std::string, std::string>& query) override;
  std::string inspectBuffer(const std::string& request_json) override;
  std::string bufferLiveData(const std::map<std::string, std::string>& query) override;
private:
  double rnd(double min, double max);
  int rndi(int min, int max);
  std::string current_node_id_;
  std::string current_node_name_;
  std::string current_module_type_;
  std::string current_buffer_key_;
  std::string current_buffer_from_;
  std::string current_buffer_to_;
  std::string last_request_json_;
  std::mutex mutex_;
  std::mt19937 rng_;
};


struct AlgorithmCostEntry {
  std::string node_id;
  double cpu = 0.0;
  int mem_kb = 0;
  double latency_ms = 0.0;
  int core = 0;
};

class IAlgorithmCostController {
public:
  virtual ~IAlgorithmCostController() = default;
  virtual std::string liveCosts(const std::map<std::string, std::string>& query) = 0;
};

class FakeAlgorithmCostController final : public IAlgorithmCostController {
public:
  FakeAlgorithmCostController();
  std::string liveCosts(const std::map<std::string, std::string>& query) override;
private:
  double rnd(double min, double max);
  int rndi(int min, int max);
  std::mutex mutex_;
  std::mt19937 rng_;
};


struct DspCoreLoadingEntry {
  int id = 0;
  double load_percent = 0.0;
  double temperature_c = 0.0;
  double power_w = 0.0;
};

struct DspCoreLoadingFrame {
  bool running = false;
  std::int64_t timestamp_ms = 0;
  int core_count = 0;
  std::vector<DspCoreLoadingEntry> cores;
  double total_load_percent = 0.0;
  double headroom_percent = 100.0;
};

class IDspCoreLoadingController {
public:
  virtual ~IDspCoreLoadingController() = default;
  virtual std::string liveCoreLoading(const std::map<std::string, std::string>& query) = 0;
};

class FakeDspCoreLoadingController final : public IDspCoreLoadingController {
public:
  FakeDspCoreLoadingController();
  std::string liveCoreLoading(const std::map<std::string, std::string>& query) override;
private:
  double rnd(double min, double max);
  int rndi(int min, int max);
  std::mutex mutex_;
  std::mt19937 rng_;
};


struct EventLogEntry {
  int id = 0;
  std::int64_t timestamp_ms = 0;
  std::string time;
  std::string kind;
  std::string message;
  std::string detail;
  std::string source;
};

class IEventLogController {
public:
  virtual ~IEventLogController() = default;
  virtual std::string postEvent(const std::string& request_json) = 0;
  virtual std::string liveEvents(const std::map<std::string, std::string>& query) = 0;
};

class FakeEventLogController final : public IEventLogController {
public:
  FakeEventLogController();
  std::string postEvent(const std::string& request_json) override;
  std::string liveEvents(const std::map<std::string, std::string>& query) override;
private:
  std::int64_t nowMs() const;
  void appendLocked(const std::string& kind, const std::string& message, const std::string& detail, const std::string& source);
  std::vector<EventLogEntry> events_;
  int next_id_ = 1;
  std::int64_t last_generated_ms_ = 0;
  std::mutex mutex_;
  std::mt19937 rng_;
};

struct SystemHealthRow {
  std::string label;
  std::string value;
  double percent = 0.0;
  std::string severity;
};

class ISystemHealthController {
public:
  virtual ~ISystemHealthController() = default;
  virtual std::string liveHealth(const std::map<std::string, std::string>& query) = 0;
};

class FakeSystemHealthController final : public ISystemHealthController {
public:
  FakeSystemHealthController();
  std::string liveHealth(const std::map<std::string, std::string>& query) override;
private:
  double rnd(double min, double max);
  int rndi(int min, int max);
  std::mutex mutex_;
  std::mt19937 rng_;
};

struct AudioIoChannelMeter {
  std::string id;
  std::string label;
  double dbfs = -120.0;
  double height = 0.0;
};

class IAudioIoController {
public:
  virtual ~IAudioIoController() = default;
  virtual std::string liveAudioIo(const std::map<std::string, std::string>& query) = 0;
};

class FakeAudioIoController final : public IAudioIoController {
public:
  FakeAudioIoController();
  std::string liveAudioIo(const std::map<std::string, std::string>& query) override;
private:
  double rnd(double min, double max);
  int rndi(int min, int max);
  std::mutex mutex_;
  std::mt19937 rng_;
};


struct RealTimeProbeChannelFrame {
  int index = 0;
  std::string label;
  std::vector<double> waveform;
  std::vector<double> spectrum;
  double rms_dbfs = -120.0;
  double peak_dbfs = -120.0;
};

class IRealTimeProbeController {
public:
  virtual ~IRealTimeProbeController() = default;
  virtual std::string configureProbe(const std::string& request_json) = 0;
  virtual std::string liveProbeData(const std::map<std::string, std::string>& query) = 0;
};

class FakeRealTimeProbeController final : public IRealTimeProbeController {
public:
  FakeRealTimeProbeController();
  std::string configureProbe(const std::string& request_json) override;
  std::string liveProbeData(const std::map<std::string, std::string>& query) override;
private:
  double rnd(double min, double max);
  int rndi(int min, int max);
  std::string last_config_json_;
  std::mutex mutex_;
  std::mt19937 rng_;
};

class MockRuntimeEngine final : public IRuntimeEngine, public INodeController, public IParameterController {
public:
  MockRuntimeEngine();
  std::string validatePipeline(const std::string& pipeline_json) override;
  std::string buildPipeline(const std::string& pipeline_json) override;
  std::string run(const std::string& session_id) override;
  std::string stop(const std::string& session_id) override;
  std::string telemetry(const std::vector<std::string>& node_ids) override;
  std::string pipelineEditEvent(const std::string& request_json) override;
  std::string pipelineToolAction(const std::string& request_json) override;
  std::string onNodeAction(const std::string& request_json) override;
  std::string updateParameter(const std::string& request_json) override;
  bool running() const { return running_.load(); }
private:
  double rnd(double min, double max);
  int rndi(int min, int max);
  std::atomic<bool> running_{false};
  std::mutex rng_mutex_;
  std::mt19937 rng_;
};

class HttpServer {
public:
  HttpServer(std::string root_dir, int port, std::shared_ptr<IRuntimeEngine> runtime,
             std::shared_ptr<INodeController> node_controller,
             std::shared_ptr<IParameterController> parameter_controller,
              std::shared_ptr<ITargetConfigController> target_config_controller = nullptr,
             std::shared_ptr<IInspectorController> inspector_controller = nullptr,
             std::shared_ptr<IAlgorithmCostController> algorithm_cost_controller = nullptr,
              std::shared_ptr<IDspCoreLoadingController> dsp_core_loading_controller = nullptr,
              std::shared_ptr<IEventLogController> event_log_controller = nullptr,
              std::shared_ptr<ISystemHealthController> system_health_controller = nullptr,
              std::shared_ptr<IAudioIoController> audio_io_controller = nullptr,
              std::shared_ptr<IRealTimeProbeController> real_time_probe_controller = nullptr);
  int run();
  HttpResponse handle(const HttpRequest& req);
private:
  std::string root_dir_;
  int port_;
  std::shared_ptr<IRuntimeEngine> runtime_;
  std::shared_ptr<INodeController> node_controller_;
  std::shared_ptr<IParameterController> parameter_controller_;
  std::shared_ptr<ITargetConfigController> target_config_controller_;
  std::shared_ptr<IInspectorController> inspector_controller_;
  std::shared_ptr<IAlgorithmCostController> algorithm_cost_controller_;
    std::shared_ptr<IDspCoreLoadingController> dsp_core_loading_controller_;
  std::shared_ptr<IEventLogController> event_log_controller_;
  std::shared_ptr<ISystemHealthController> system_health_controller_;
  std::shared_ptr<IAudioIoController> audio_io_controller_;
  std::shared_ptr<IRealTimeProbeController> real_time_probe_controller_;
HttpResponse serveFile(const std::string& rel_path, const std::string& content_type);
};

std::string readFile(const std::string& path);
std::vector<std::string> splitCsv(const std::string& csv);
std::string jsonEscape(const std::string& s);
std::string contentTypeForPath(const std::string& path);
std::map<std::string, std::string> parseQuery(const std::string& q);

} // namespace audiostudio
