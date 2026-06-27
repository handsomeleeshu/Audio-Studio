#include "audio_studio.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct BackendOptions {
  std::string root = ".";
  int http_port = 8080;
  audiostudio::BackendRuntimeConfig runtime;
};

bool isOption(const std::string& value) {
  return value.size() > 2 && value[0] == '-' && value[1] == '-';
}

uint16_t parsePort(const std::string& value, const std::string& option) {
  const int parsed = std::stoi(value);
  if (parsed <= 0 || parsed > 65535) {
    throw std::invalid_argument(option + " must be in range 1..65535");
  }
  return static_cast<uint16_t>(parsed);
}

uint32_t parseU32(const std::string& value, const std::string& option) {
  const unsigned long parsed = std::stoul(value);
  if (parsed > 0xffffffffUL) throw std::invalid_argument(option + " is too large");
  return static_cast<uint32_t>(parsed);
}

long parsePositiveLong(const std::string& value, const std::string& option) {
  const long parsed = std::stol(value);
  if (parsed <= 0) throw std::invalid_argument(option + " must be positive");
  return parsed;
}

bool parseBool(const std::string& value, const std::string& option) {
  if (value == "true" || value == "1" || value == "yes") return true;
  if (value == "false" || value == "0" || value == "no") return false;
  throw std::invalid_argument(option + " must be true or false");
}

void printUsage(const char* program) {
  std::cout
      << "usage: " << program << " [root] [port] [options]\n"
      << "options:\n"
      << "  --root PATH\n"
      << "  --port PORT\n"
      << "  --as-server PATH\n"
      << "  --alsatplg PATH\n"
      << "  --as-server-rpc-mode once|socket\n"
      << "  --as-server-host HOST\n"
      << "  --as-server-port PORT\n"
      << "  --as-server-timeout-ms MS\n"
      << "  --validation-python PYTHON\n"
      << "  --validation-script PATH\n"
      << "  --validation-as-server PATH\n"
      << "  --validation-as-log PATH\n"
      << "  --validation-trace-ldc PATH\n"
      << "  --validation-as-server-host HOST\n"
      << "  --validation-as-server-port PORT\n"
      << "  --validation-ready-timeout-ms MS\n"
      << "  --validation-use-existing-as-server BOOL\n"
      << "  --validation-datalink PATH\n"
      << "  --validation-qemu-gdb-port PORT\n"
      << "  --validation-qemu-gdb-wait BOOL\n"
      << "  --runtime-as-server-host HOST\n"
      << "  --runtime-as-server-port PORT\n"
      << "  --audio-driver-factory NAME\n";
}

int parseBackendOptions(int argc, char** argv, BackendOptions& options) {
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      return 1;
    }
    if (!isOption(arg)) {
      positional.push_back(arg);
      continue;
    }
    if (i + 1 >= argc) throw std::invalid_argument("missing value for " + arg);
    const std::string value = argv[++i];
    if (arg == "--root") options.root = value;
    else if (arg == "--port") options.http_port = parsePort(value, arg);
    else if (arg == "--as-server") options.runtime.compile_as_server_path = value;
    else if (arg == "--alsatplg") options.runtime.compile_alsatplg_path = value;
    else if (arg == "--as-server-rpc-mode") options.runtime.compile_as_server_rpc_mode = value;
    else if (arg == "--as-server-host") options.runtime.compile_as_server_host = value;
    else if (arg == "--as-server-port") options.runtime.compile_as_server_port = parsePort(value, arg);
    else if (arg == "--as-server-timeout-ms") options.runtime.compile_as_server_timeout_ms = parseU32(value, arg);
    else if (arg == "--validation-python") options.runtime.validation_python = value;
    else if (arg == "--validation-script") options.runtime.validation_script_path = value;
    else if (arg == "--validation-as-server") options.runtime.validation_as_server_path = value;
    else if (arg == "--validation-as-log") options.runtime.validation_as_log_path = value;
    else if (arg == "--validation-trace-ldc") options.runtime.validation_trace_ldc_path = value;
    else if (arg == "--validation-as-server-host") options.runtime.validation_as_server_host = value;
    else if (arg == "--validation-as-server-port") options.runtime.validation_as_server_port = parsePort(value, arg);
    else if (arg == "--validation-ready-timeout-ms") options.runtime.validation_ready_timeout_ms = parsePositiveLong(value, arg);
    else if (arg == "--validation-use-existing-as-server") options.runtime.validation_use_existing_as_server = parseBool(value, arg);
    else if (arg == "--validation-datalink") options.runtime.validation_datalink_endpoint = value;
    else if (arg == "--validation-qemu-gdb-port") options.runtime.validation_qemu_gdb_port = parsePort(value, arg);
    else if (arg == "--validation-qemu-gdb-wait") options.runtime.validation_qemu_gdb_wait = parseBool(value, arg);
    else if (arg == "--runtime-as-server-host") options.runtime.runtime_as_server_host = value;
    else if (arg == "--runtime-as-server-port") options.runtime.runtime_as_server_port = parsePort(value, arg);
    else if (arg == "--audio-driver-factory") options.runtime.runtime_audio_driver_factory = value;
    else throw std::invalid_argument("unknown option: " + arg);
  }
  if (!positional.empty()) options.root = positional[0];
  if (positional.size() > 1) options.http_port = parsePort(positional[1], "port");
  if (positional.size() > 2) throw std::invalid_argument("too many positional arguments");
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  BackendOptions options;
  try {
    const int parse_result = parseBackendOptions(argc, argv, options);
    if (parse_result != 0) return 0;
  } catch (const std::exception& e) {
    std::cerr << "argument error: " << e.what() << std::endl;
    return 2;
  }

  auto build_orchestrator = std::make_shared<audiostudio::BuildOrchestrator>(
      options.root, nullptr, nullptr, options.runtime);
  auto runtime = std::make_shared<audiostudio::GuiRuntimeEngine>(build_orchestrator, options.runtime);
  auto node_controls = std::make_shared<audiostudio::MockRuntimeEngine>();
  auto target_config = std::make_shared<audiostudio::FakeTargetConfigController>();
  auto inspector = std::make_shared<audiostudio::FakeInspectorController>();
  auto algorithm_cost = std::make_shared<audiostudio::RpcAlgorithmCostController>(
      options.runtime.runtime_as_server_host, options.runtime.runtime_as_server_port);
  auto dsp_core_loading = std::make_shared<audiostudio::RpcDspCoreLoadingController>(
      options.runtime.runtime_as_server_host, options.runtime.runtime_as_server_port);
  auto event_log = std::make_shared<audiostudio::FakeEventLogController>();
  auto system_health = std::make_shared<audiostudio::RpcSystemHealthController>(
      options.runtime.runtime_as_server_host, options.runtime.runtime_as_server_port);
  auto audio_io = std::make_shared<audiostudio::FakeAudioIoController>();
  auto real_time_probe = std::make_shared<audiostudio::FakeRealTimeProbeController>();
  audiostudio::HttpServer server(options.root, options.http_port, runtime, node_controls, node_controls, target_config,
                                 inspector, algorithm_cost, dsp_core_loading, event_log,
                                 system_health, audio_io, real_time_probe, build_orchestrator);
  try { return server.run(); }
  catch (const std::exception& e) { std::cerr << "server error: " << e.what() << std::endl; return 1; }
}
