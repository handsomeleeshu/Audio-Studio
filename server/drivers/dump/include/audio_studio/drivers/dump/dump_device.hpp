#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::drivers::dump {

struct DumpPoint {
  uint32_t id = 0;
  std::string name;
};

struct DumpPacket {
  uint32_t point_id = 0;
  std::vector<uint8_t> bytes;
};

struct DumpStats {
  size_t packets_written = 0;
  size_t packets_read = 0;
  bool running = false;
};

class DumpDevice {
public:
  framework::Status open(std::string device);
  framework::Status addPoint(DumpPoint point);
  framework::Status removePoint(uint32_t point_id);
  std::vector<DumpPoint> listPoints() const;
  framework::Status start();
  framework::Status appendPacket(DumpPacket packet);
  framework::Status readPacket(DumpPacket& out);
  framework::Status stop();
  void close();
  DumpStats stats() const;

private:
  std::string device_;
  bool open_ = false;
  bool running_ = false;
  size_t packets_written_ = 0;
  size_t packets_read_ = 0;
  std::vector<DumpPoint> points_;
  std::vector<DumpPacket> packets_;
};

} // namespace audio_studio::drivers::dump
