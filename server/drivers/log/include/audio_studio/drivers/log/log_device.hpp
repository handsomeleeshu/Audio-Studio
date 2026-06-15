#pragma once

#include <string>
#include <vector>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::drivers::log {

struct LogChunk {
  uint32_t sequence = 0;
  std::vector<uint8_t> bytes;
};

struct LogStats {
  size_t chunks_written = 0;
  size_t chunks_read = 0;
  bool running = false;
};

class LogDevice {
public:
  framework::Status open(std::string source);
  framework::Status start();
  framework::Status appendChunk(LogChunk chunk);
  framework::Status readChunk(LogChunk& out);
  framework::Status stop();
  void close();
  LogStats stats() const;

private:
  std::string source_;
  bool open_ = false;
  bool running_ = false;
  size_t chunks_written_ = 0;
  size_t chunks_read_ = 0;
  std::vector<LogChunk> chunks_;
};

} // namespace audio_studio::drivers::log
