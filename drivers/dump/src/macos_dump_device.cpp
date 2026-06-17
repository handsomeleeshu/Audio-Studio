#include "macos_dump_device.hpp"

#include <algorithm>
#include <utility>

namespace audio_studio::drivers::dump {

namespace {

class MacOsDumpDeviceFactory final : public IDumpDeviceFactory {
public:
  std::string name() const override { return "macos"; }
  std::unique_ptr<IDumpDevice> create(const DumpDeviceConfig& config) const override {
    auto device = std::make_unique<MacOsDumpDevice>();
    if (!device->open(config).ok()) return nullptr;
    return device;
  }
};

const bool kMacOsDumpDeviceRegistered = [] {
  auto status = DumpDeviceRegistry::instance().registerFactory(std::make_unique<MacOsDumpDeviceFactory>());
  (void)status;
  return true;
}();

} // namespace

DumpResult MacOsDumpDevice::open(const DumpDeviceConfig& config) {
  if (config.device.empty()) return DumpResult::invalidArgument("dump device is empty");
  device_config_ = config;
  open_ = true;
  return DumpResult::success();
}

DumpResult MacOsDumpDevice::configure(const DumpSessionConfig& config) {
  if (!open_) return DumpResult::unavailable("dump device is not open");
  session_config_ = config;
  return DumpResult::success();
}

DumpResult MacOsDumpDevice::listPoints(std::vector<DumpPointInfo>& points) {
  if (!open_) return DumpResult::unavailable("dump device is not open");
  points = points_;
  return DumpResult::success();
}

DumpResult MacOsDumpDevice::addPoint(const ProbePoint& point) {
  if (!open_) return DumpResult::unavailable("dump device is not open");
  if (point.point_id == 0) return DumpResult::invalidArgument("dump point id is zero");
  if (point.name.empty()) return DumpResult::invalidArgument("dump point name is empty");
  const auto exists = std::any_of(points_.begin(), points_.end(), [&](const DumpPointInfo& item) {
    return item.point_id == point.point_id;
  });
  if (exists) return DumpResult::invalidArgument("dump point already exists");
  points_.push_back({point.point_id, point.name});
  return DumpResult::success();
}

DumpResult MacOsDumpDevice::removePoint(uint32_t point_id) {
  const auto old_size = points_.size();
  points_.erase(std::remove_if(points_.begin(), points_.end(), [&](const DumpPointInfo& item) {
                  return item.point_id == point_id;
                }),
                points_.end());
  if (points_.size() == old_size) return DumpResult::unavailable("dump point not found");
  return DumpResult::success();
}

DumpResult MacOsDumpDevice::removeAllPoints() {
  points_.clear();
  return DumpResult::success();
}

DumpResult MacOsDumpDevice::start() {
  if (!open_) return DumpResult::unavailable("dump device is not open");
  if (packets_.empty() && !points_.empty()) {
    packets_.push_back({points_.front().point_id, {0x10, 0x20}});
    ++packets_written_;
  }
  running_ = true;
  return DumpResult::success();
}

DumpResult MacOsDumpDevice::stop() {
  if (!open_) return DumpResult::unavailable("dump device is not open");
  running_ = false;
  return DumpResult::success();
}

DumpResult MacOsDumpDevice::readPacket(DumpRawPacket& packet, uint32_t /*timeout_ms*/) {
  if (!running_) return DumpResult::unavailable("dump device is not running");
  if (packets_.empty()) return DumpResult::unavailable("no dump packet available");
  packet = std::move(packets_.front());
  packets_.erase(packets_.begin());
  ++packets_read_;
  return DumpResult::success();
}

DumpResult MacOsDumpDevice::getStats(DumpDeviceStats& stats) {
  stats = {packets_written_, packets_read_, running_};
  return DumpResult::success();
}

void MacOsDumpDevice::close() {
  open_ = false;
  running_ = false;
  points_.clear();
  packets_.clear();
}

DumpResult MacOsDumpDevice::appendPacket(DumpRawPacket packet) {
  if (!open_) return DumpResult::unavailable("dump device is not open");
  if (packet.point_id == 0) return DumpResult::invalidArgument("dump packet point id is zero");
  if (packet.bytes.empty()) return DumpResult::invalidArgument("dump packet is empty");
  packets_.push_back(std::move(packet));
  ++packets_written_;
  return DumpResult::success();
}

} // namespace audio_studio::drivers::dump
