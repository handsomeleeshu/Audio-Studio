#include "audio_studio.hpp"
#include <iostream>

int main(int argc, char** argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  const int port = argc > 2 ? std::stoi(argv[2]) : 8080;
  auto runtime = std::make_shared<audiostudio::MockRuntimeEngine>();
  auto inspector = std::make_shared<audiostudio::FakeInspectorController>();
  auto algorithm_cost = std::make_shared<audiostudio::FakeAlgorithmCostController>();
  auto dsp_core_loading = std::make_shared<audiostudio::FakeDspCoreLoadingController>();
  auto event_log = std::make_shared<audiostudio::FakeEventLogController>();
  auto system_health = std::make_shared<audiostudio::FakeSystemHealthController>();
  auto audio_io = std::make_shared<audiostudio::FakeAudioIoController>();
  audiostudio::HttpServer server(root, port, runtime, runtime, runtime, inspector, algorithm_cost, dsp_core_loading, event_log, system_health, audio_io);
  try { return server.run(); }
  catch (const std::exception& e) { std::cerr << "server error: " << e.what() << std::endl; return 1; }
}
