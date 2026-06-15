#include <iostream>
#include <string>

#include "audio_studio/drivers/dummy/dummy_driver.hpp"
#include "audio_studio/framework/service_registry.hpp"

namespace {

audio_studio::framework::ServiceRegistry defaultRegistry() {
  audio_studio::framework::ServiceRegistry registry;
  registry.registerService("health", "server health service");
  registry.registerService("dummy", "host-alone dummy driver service");
  return registry;
}

int printServices() {
  const auto registry = defaultRegistry();
  std::cout << "{\"services\":[";
  const auto names = registry.serviceNames();
  for (size_t i = 0; i < names.size(); ++i) {
    if (i) std::cout << ",";
    std::cout << "{\"name\":\"" << names[i] << "\",\"description\":\"" << registry.describe(names[i]) << "\"}";
  }
  std::cout << "]}\n";
  return 0;
}

int runSelfTest() {
  audio_studio::drivers::dummy::DummyDriver driver;
  auto status = driver.open();
  if (!status.ok()) {
    std::cerr << status.toJson() << "\n";
    return 1;
  }
  status = driver.start();
  if (!status.ok()) {
    std::cerr << status.toJson() << "\n";
    return 1;
  }
  driver.sendCommand("ping");
  driver.stop();
  const auto telemetry = driver.telemetry();
  std::cout << "{\"ok\":true,\"driver\":\"dummy\",\"frames\":" << telemetry.frames_processed
            << ",\"commands\":" << telemetry.commands_processed << "}\n";
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  const std::string arg = argc > 1 ? argv[1] : "--list-services";
  if (arg == "--version") {
    std::cout << "Audio Studio as_server host-alone dummy\n";
    return 0;
  }
  if (arg == "--list-services") return printServices();
  if (arg == "--self-test") return runSelfTest();
  std::cerr << "usage: as_server [--version|--list-services|--self-test]\n";
  return 2;
}
