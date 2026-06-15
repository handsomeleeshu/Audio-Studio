#include <cassert>
#include <iostream>

#include "audio_studio/drivers/dummy/dummy_driver.hpp"
#include "audio_studio/framework/audio/audio_service.hpp"
#include "audio_studio/framework/control/control_service.hpp"
#include "audio_studio/framework/log/log_service.hpp"
#include "audio_studio/framework/session/session_registry.hpp"
#include "audio_studio/framework/service_registry.hpp"
#include "audio_studio/framework/status.hpp"
#include "audio_studio/rpc/json_rpc.hpp"

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

  audio_studio::framework::session::SessionRegistry sessions;
  assert(sessions.create("sess-1", "server_tests").ok());
  assert(sessions.has("sess-1"));
  assert(sessions.get("sess-1").active);
  assert(sessions.activeCount() == 1);
  assert(!sessions.create("sess-1", "duplicate").ok());
  assert(sessions.close("sess-1").ok());
  assert(!sessions.get("sess-1").active);
  assert(sessions.activeCount() == 0);

  audio_studio::framework::control::ControlService controls;
  assert(controls.set({"AEC", "echo_suppress_db", "-20"}).ok());
  assert(controls.size() == 1);
  audio_studio::framework::control::ControlValue control_value;
  assert(controls.get("AEC", "echo_suppress_db", control_value).ok());
  assert(control_value.value == "-20");
  assert(!controls.get("AEC", "missing", control_value).ok());

  audio_studio::framework::audio::AudioService audio;
  assert(audio.create({"playback-1", audio_studio::framework::audio::StreamDirection::kPlayback, 48000, 2, false}).ok());
  assert(audio.start("playback-1").ok());
  audio_studio::framework::audio::AudioStream stream;
  assert(audio.get("playback-1", stream).ok());
  assert(stream.running);
  assert(audio.stop("playback-1").ok());
  assert(!audio.create({"bad", audio_studio::framework::audio::StreamDirection::kCapture, 0, 2, false}).ok());

  audio_studio::framework::log::LogService logs;
  assert(logs.append("info", "server started").ok());
  assert(logs.append("warn", "dummy warning").ok());
  assert(logs.tail(1).front().message == "dummy warning");
  assert(logs.size() == 2);
  logs.clear();
  assert(logs.size() == 0);

  audio_studio::rpc::JsonRpcRequest request;
  assert(audio_studio::rpc::parseRequest(R"({"jsonrpc":"2.0","id":"1","method":"health.ping","params":{"x":1}})", request).ok());
  assert(request.id == "1");
  assert(request.method == "health.ping");
  assert(request.params_json.find("\"x\":1") != std::string::npos);
  assert(audio_studio::rpc::resultResponse("1", R"({"pong":true})").find("\"result\"") != std::string::npos);
  assert(audio_studio::rpc::errorResponse("1", -32601, "missing").find("\"error\"") != std::string::npos);

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
