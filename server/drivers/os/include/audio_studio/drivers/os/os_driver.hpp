#pragma once

#include <map>
#include <string>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::drivers::os {

struct OsSystemInfo {
  std::string platform = "host-alone";
  unsigned cpu_count = 1;
  uint64_t pid = 1;
};

class OsDriver {
public:
  uint64_t nowMs() const;
  framework::Status sleepForMs(uint64_t duration_ms);
  framework::Status setEnv(std::string key, std::string value);
  framework::Status getEnv(const std::string& key, std::string& out) const;
  OsSystemInfo systemInfo() const;

private:
  uint64_t monotonic_ms_ = 0;
  std::map<std::string, std::string> env_;
};

} // namespace audio_studio::drivers::os
