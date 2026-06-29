#include "audio_studio.hpp"

#include <CLI/CLI.hpp>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct BackendOptions {
  std::string root = ".";
  uint16_t http_port = 8080;
  audiostudio::BackendRuntimeConfig runtime;
};

uint16_t checkedPort(int value, const std::string& option, bool allow_zero = false) {
  const int min_value = allow_zero ? 0 : 1;
  if (value < min_value || value > 65535) {
    throw CLI::ValidationError(option + " must be in range " + std::to_string(min_value) + "..65535");
  }
  return static_cast<uint16_t>(value);
}

uint32_t checkedU32(unsigned long long value, const std::string& option) {
  if (value > 0xffffffffULL) throw CLI::ValidationError(option + " is too large");
  return static_cast<uint32_t>(value);
}

int parseInt(const std::string& value, const std::string& option) {
  try {
    size_t consumed = 0;
    const int parsed = std::stoi(value, &consumed);
    if (consumed != value.size()) throw std::invalid_argument("trailing characters");
    return parsed;
  } catch (const std::exception&) {
    throw CLI::ValidationError(option + " must be an integer");
  }
}

int parseBackendOptions(int argc, char** argv, BackendOptions& options) {
  std::vector<std::string> positional;
  int http_port = options.http_port;
  int as_server_port = options.runtime.as_server_port;
  int qemu_gdb_port = options.runtime.qemu_gdb_port;
  unsigned long long as_server_timeout_ms = options.runtime.as_server_timeout_ms;

  CLI::App app{"Audio Studio GUI backend", "audio_studio_gui_server"};
  app.option_defaults()->always_capture_default();
  app.add_option("args", positional, "Optional positional args: [root] [port]")->expected(0, 2);
  app.add_option("--root", options.root, "Audio Studio root directory");
  app.add_option("--port", http_port, "HTTP server port");
  app.add_option("--as-server", options.runtime.as_server_path, "as_server executable used by the build helper");
  app.add_option("--alsatplg", options.runtime.alsatplg_path, "alsatplg executable used by config.compile");
  app.add_option("--as-server-host", options.runtime.as_server_host, "as_server RPC host");
  app.add_option("--as-server-port", as_server_port, "as_server RPC port");
  app.add_option("--as-server-timeout-ms", as_server_timeout_ms, "as_server RPC connection timeout");
  app.add_option("--helper-python", options.runtime.helper_python, "Python executable used to start the simulator helper");
  app.add_option("--helper-script", options.runtime.helper_script_path, "Simulator helper script path");
  app.add_option("--as-log", options.runtime.as_log_path, "as_log executable used by the simulator helper");
  app.add_option("--trace-ldc", options.runtime.trace_ldc_path, "SOF trace LDC dictionary used by the helper as_server");
  app.add_option("--ready-timeout-ms", options.runtime.ready_timeout_ms, "Build helper ready timeout in milliseconds");
  app.add_option("--datalink", options.runtime.datalink_endpoint, "Simulator datalink endpoint prefix");
  app.add_option("--qemu-gdb-port", qemu_gdb_port, "QEMU gdbstub port. Zero disables QEMU debug");
  app.add_option("--qemu-gdb-wait", options.runtime.qemu_gdb_wait, "Start QEMU stopped for debugger attach");
  app.add_option("--audio-driver-factory", options.runtime.runtime_audio_driver_factory, "Default runtime audio driver factory");
  app.allow_extras(false);

  try {
    app.parse(argc, argv);
    if (!positional.empty()) options.root = positional[0];
    if (positional.size() > 1) http_port = parseInt(positional[1], "port");
    options.http_port = checkedPort(http_port, "--port");
    options.runtime.as_server_port = checkedPort(as_server_port, "--as-server-port");
    options.runtime.qemu_gdb_port = checkedPort(qemu_gdb_port, "--qemu-gdb-port", true);
    options.runtime.as_server_timeout_ms = checkedU32(as_server_timeout_ms, "--as-server-timeout-ms");
    if (options.runtime.ready_timeout_ms <= 0) {
      throw CLI::ValidationError("--ready-timeout-ms must be positive");
    }
  } catch (const CLI::ParseError& error) {
    return app.exit(error);
  }
  return -1;
}

} // namespace

int main(int argc, char** argv) {
  BackendOptions options;
  const int parse_result = parseBackendOptions(argc, argv, options);
  if (parse_result >= 0) return parse_result;

  auto build_orchestrator = std::make_shared<audiostudio::BuildOrchestrator>(
      options.root, nullptr, nullptr, options.runtime);
  auto runtime = std::make_shared<audiostudio::GuiRuntimeEngine>(build_orchestrator, options.runtime);
  auto node_controls = std::make_shared<audiostudio::MockRuntimeEngine>();
  auto target_config = std::make_shared<audiostudio::FakeTargetConfigController>();
  auto inspector = std::make_shared<audiostudio::FakeInspectorController>();
  auto algorithm_cost = std::make_shared<audiostudio::RpcAlgorithmCostController>(
      options.runtime.as_server_host, options.runtime.as_server_port);
  auto dsp_core_loading = std::make_shared<audiostudio::RpcDspCoreLoadingController>(
      options.runtime.as_server_host, options.runtime.as_server_port);
  auto event_log = std::make_shared<audiostudio::FakeEventLogController>();
  auto system_health = std::make_shared<audiostudio::RpcSystemHealthController>(
      options.runtime.as_server_host, options.runtime.as_server_port);
  auto audio_io = std::make_shared<audiostudio::FakeAudioIoController>();
  auto real_time_probe = std::make_shared<audiostudio::FakeRealTimeProbeController>();
  audiostudio::HttpServer server(options.root, options.http_port, runtime, node_controls, node_controls, target_config,
                                 inspector, algorithm_cost, dsp_core_loading, event_log,
                                 system_health, audio_io, real_time_probe, build_orchestrator);
  try { return server.run(); }
  catch (const std::exception& e) { std::cerr << "server error: " << e.what() << std::endl; return 1; }
}
