#include "audio_studio.hpp"
#include <iostream>

int main(int argc, char** argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  const int port = argc > 2 ? std::stoi(argv[2]) : 8080;
  auto build_orchestrator = std::make_shared<audiostudio::BuildOrchestrator>(root);
  auto runtime = std::make_shared<audiostudio::GuiRuntimeEngine>(build_orchestrator);
  auto node_controls = std::make_shared<audiostudio::MockRuntimeEngine>();
  auto target_config = std::make_shared<audiostudio::FakeTargetConfigController>();
  auto inspector = std::make_shared<audiostudio::FakeInspectorController>();
  auto algorithm_cost = std::make_shared<audiostudio::RpcAlgorithmCostController>();
  auto dsp_core_loading = std::make_shared<audiostudio::RpcDspCoreLoadingController>();
  auto event_log = std::make_shared<audiostudio::FakeEventLogController>();
  auto system_health = std::make_shared<audiostudio::RpcSystemHealthController>();
  auto audio_io = std::make_shared<audiostudio::FakeAudioIoController>();
  auto real_time_probe = std::make_shared<audiostudio::FakeRealTimeProbeController>();
  audiostudio::HttpServer server(root, port, runtime, node_controls, node_controls, target_config,
                                 inspector, algorithm_cost, dsp_core_loading, event_log,
                                 system_health, audio_io, real_time_probe, build_orchestrator);
  try { return server.run(); }
  catch (const std::exception& e) { std::cerr << "server error: " << e.what() << std::endl; return 1; }
}
