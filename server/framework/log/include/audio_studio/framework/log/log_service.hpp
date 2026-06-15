#pragma once

#include <string>
#include <vector>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::framework::log {

struct LogEntry {
  int sequence = 0;
  std::string level;
  std::string message;
};

class LogService {
public:
  framework::Status append(std::string level, std::string message);
  std::vector<LogEntry> tail(size_t count) const;
  void clear();
  size_t size() const;

private:
  int next_sequence_ = 1;
  std::vector<LogEntry> entries_;
};

} // namespace audio_studio::framework::log
