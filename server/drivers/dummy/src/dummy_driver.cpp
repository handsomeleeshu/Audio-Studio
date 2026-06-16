#include "dummy_driver.hpp"

namespace audio_studio::drivers::dummy {

framework::Status DummyDriver::open() {
  if (opened_) return framework::Status::success();
  opened_ = true;
  return framework::Status::success();
}

framework::Status DummyDriver::close() {
  running_ = false;
  opened_ = false;
  return framework::Status::success();
}

framework::Status DummyDriver::start() {
  if (!opened_) return framework::Status::unavailable("dummy driver is not open");
  running_ = true;
  frames_processed_ += 48;
  return framework::Status::success();
}

framework::Status DummyDriver::stop() {
  if (!opened_) return framework::Status::unavailable("dummy driver is not open");
  running_ = false;
  return framework::Status::success();
}

framework::Status DummyDriver::sendCommand(const std::string& command) {
  if (!opened_) return framework::Status::unavailable("dummy driver is not open");
  if (command.empty()) return framework::Status::invalidArgument("dummy command is empty");
  commands_.push_back(command);
  if (running_) frames_processed_ += 48;
  return framework::Status::success();
}

DummyTelemetry DummyDriver::telemetry() const {
  return {frames_processed_, static_cast<int>(commands_.size()), running_};
}

std::vector<std::string> DummyDriver::commandLog() const {
  return commands_;
}

} // namespace audio_studio::drivers::dummy
