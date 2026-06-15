#include "audio_studio/drivers/os/os_driver.hpp"

#include <utility>

namespace audio_studio::drivers::os {

uint64_t OsDriver::nowMs() const {
  return monotonic_ms_;
}

framework::Status OsDriver::sleepForMs(uint64_t duration_ms) {
  monotonic_ms_ += duration_ms;
  return framework::Status::success();
}

framework::Status OsDriver::setEnv(std::string key, std::string value) {
  if (key.empty()) return framework::Status::invalidArgument("env key is empty");
  env_[std::move(key)] = std::move(value);
  return framework::Status::success();
}

framework::Status OsDriver::getEnv(const std::string& key, std::string& out) const {
  const auto it = env_.find(key);
  if (it == env_.end()) return framework::Status::unavailable("env key not found: " + key);
  out = it->second;
  return framework::Status::success();
}

OsSystemInfo OsDriver::systemInfo() const {
  return {};
}

} // namespace audio_studio::drivers::os
