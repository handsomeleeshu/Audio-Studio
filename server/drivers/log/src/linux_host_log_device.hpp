#pragma once

#include "log_device.hpp"

namespace audio_studio::drivers::log {

class LinuxHostLogDevice final : public ILogDevice {
public:
  LogResult open(const LogDeviceConfig& config) override;
  LogResult configure(const LogDeviceConfig& config) override;
  LogResult start() override;
  LogResult stop() override;
  LogResult readChunk(LogRawChunk& chunk, uint32_t timeout_ms) override;
  LogResult getStats(LogDeviceStats& stats) override;
  void close() override;

  LogResult appendChunk(LogRawChunk chunk);

private:
  LogDeviceConfig config_;
  bool open_ = false;
  bool running_ = false;
  size_t chunks_written_ = 0;
  size_t chunks_read_ = 0;
  std::vector<LogRawChunk> chunks_;
};

} // namespace audio_studio::drivers::log
