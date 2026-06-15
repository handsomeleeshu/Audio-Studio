#include <cassert>
#include <iostream>

#include "audio_studio/drivers/dummy/dummy_driver.hpp"
#include "audio_studio/framework/service_registry.hpp"
#include "audio_studio/framework/status.hpp"

int main() {
  using audio_studio::drivers::dummy::DummyDriver;
  using audio_studio::framework::ServiceRegistry;
  using audio_studio::framework::Status;

  auto ok = Status::success();
  assert(ok.ok());
  assert(ok.codeString() == "OK");
  assert(ok.toJson().find("\"ok\":true") != std::string::npos);

  ServiceRegistry registry;
  assert(registry.registerService("dummy", "dummy driver").ok());
  assert(registry.hasService("dummy"));
  assert(registry.describe("dummy") == "dummy driver");
  assert(!registry.registerService("", "bad").ok());
  assert(!registry.registerService("dummy", "duplicate").ok());

  DummyDriver driver;
  assert(!driver.start().ok());
  assert(driver.open().ok());
  assert(driver.start().ok());
  assert(driver.sendCommand("ping").ok());
  assert(!driver.sendCommand("").ok());
  assert(driver.stop().ok());
  const auto telemetry = driver.telemetry();
  assert(telemetry.frames_processed >= 96);
  assert(telemetry.commands_processed == 1);
  assert(!telemetry.running);
  assert(driver.commandLog().front() == "ping");
  assert(driver.close().ok());

  std::cout << "server_tests passed\n";
  return 0;
}
