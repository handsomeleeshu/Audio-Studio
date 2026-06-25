#include "audio_studio.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

uint16_t envPort(const char* name, uint16_t fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') return fallback;
  try {
    const int parsed = std::stoi(value);
    if (parsed > 0 && parsed <= 65535) return static_cast<uint16_t>(parsed);
  } catch (...) {
  }
  return fallback;
}

} // namespace

int main(int argc, char** argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  const int port = argc > 2 ? std::stoi(argv[2]) : 8080;
  const uint16_t system_info_port = envPort("AUDIO_STUDIO_VALIDATION_AS_SERVER_PORT", 9901);
  auto build_orchestrator = std::make_shared<audiostudio::BuildOrchestrator>(root);
  auto runtime = std::make_shared<audiostudio::GuiRuntimeEngine>(build_orchestrator);
  auto node_controls = std::make_shared<audiostudio::MockRuntimeEngine>();
  auto target_config = std::make_shared<audiostudio::FakeTargetConfigController>();
  auto inspector = std::make_shared<audiostudio::FakeInspectorController>();
  auto algorithm_cost = std::make_shared<audiostudio::RpcAlgorithmCostController>("127.0.0.1", system_info_port);
  auto dsp_core_loading = std::make_shared<audiostudio::RpcDspCoreLoadingController>("127.0.0.1", system_info_port);
  auto event_log = std::make_shared<audiostudio::FakeEventLogController>();
  auto system_health = std::make_shared<audiostudio::RpcSystemHealthController>("127.0.0.1", system_info_port);
  auto audio_io = std::make_shared<audiostudio::FakeAudioIoController>();
  auto real_time_probe = std::make_shared<audiostudio::FakeRealTimeProbeController>();
  audiostudio::HttpServer server(root, port, runtime, node_controls, node_controls, target_config,
                                 inspector, algorithm_cost, dsp_core_loading, event_log,
                                 system_health, audio_io, real_time_probe, build_orchestrator);
  try { return server.run(); }
  catch (const std::exception& e) { std::cerr << "server error: " << e.what() << std::endl; return 1; }
}
