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
             std::shared_ptr<IParameterController> parameter_controller);
  int run();
  HttpResponse handle(const HttpRequest& req);
private:
  std::string root_dir_;
  int port_;
  std::shared_ptr<IRuntimeEngine> runtime_;
  std::shared_ptr<INodeController> node_controller_;
  std::shared_ptr<IParameterController> parameter_controller_;
  HttpResponse serveFile(const std::string& rel_path, const std::string& content_type);
};

std::string readFile(const std::string& path);
std::vector<std::string> splitCsv(const std::string& csv);
std::string jsonEscape(const std::string& s);
std::string contentTypeForPath(const std::string& path);
std::map<std::string, std::string> parseQuery(const std::string& q);

} // namespace audiostudio
