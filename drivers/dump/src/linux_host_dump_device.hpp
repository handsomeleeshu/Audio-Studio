#pragma once

#include "dump_device.hpp"

namespace audio_studio::drivers::dump {

class LinuxHostDumpDevice final : public IDumpDevice {
public:
  DumpResult open(const DumpDeviceConfig& config) override;
  DumpResult configure(const DumpSessionConfig& config) override;
  DumpResult listPoints(std::vector<DumpPointInfo>& points) override;
  DumpResult addPoint(const ProbePoint& point) override;
  DumpResult removePoint(uint32_t point_id) override;
  DumpResult removeAllPoints() override;
  DumpResult start() override;
  DumpResult stop() override;
  DumpResult readPacket(DumpRawPacket& packet, uint32_t timeout_ms) override;
  DumpResult getStats(DumpDeviceStats& stats) override;
  void close() override;

  DumpResult appendPacket(DumpRawPacket packet);

private:
  DumpDeviceConfig device_config_;
  DumpSessionConfig session_config_;
  bool open_ = false;
  bool running_ = false;
  size_t packets_written_ = 0;
  size_t packets_read_ = 0;
  std::vector<DumpPointInfo> points_;
  std::vector<DumpRawPacket> packets_;
};

} // namespace audio_studio::drivers::dump
