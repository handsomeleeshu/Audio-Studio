#include "audio_studio/drivers/dump/dump_device.hpp"

#include <algorithm>
#include <utility>

namespace audio_studio::drivers::dump {

framework::Status DumpDevice::open(std::string device) {
  if (device.empty()) return framework::Status::invalidArgument("dump device is empty");
  device_ = std::move(device);
  open_ = true;
  return framework::Status::success();
}

framework::Status DumpDevice::addPoint(DumpPoint point) {
  if (!open_) return framework::Status::unavailable("dump device is not open");
  if (point.id == 0) return framework::Status::invalidArgument("dump point id is zero");
  if (point.name.empty()) return framework::Status::invalidArgument("dump point name is empty");
  const auto exists = std::any_of(points_.begin(), points_.end(), [&](const DumpPoint& item) { return item.id == point.id; });
  if (exists) return framework::Status::invalidArgument("dump point already exists");
  points_.push_back(std::move(point));
  return framework::Status::success();
}

framework::Status DumpDevice::removePoint(uint32_t point_id) {
  const auto old_size = points_.size();
  points_.erase(std::remove_if(points_.begin(), points_.end(), [&](const DumpPoint& item) { return item.id == point_id; }), points_.end());
  if (points_.size() == old_size) return framework::Status::unavailable("dump point not found");
  return framework::Status::success();
}

std::vector<DumpPoint> DumpDevice::listPoints() const {
  return points_;
}

framework::Status DumpDevice::start() {
  if (!open_) return framework::Status::unavailable("dump device is not open");
  running_ = true;
  return framework::Status::success();
}

framework::Status DumpDevice::appendPacket(DumpPacket packet) {
  if (!open_) return framework::Status::unavailable("dump device is not open");
  if (packet.point_id == 0) return framework::Status::invalidArgument("dump packet point id is zero");
  if (packet.bytes.empty()) return framework::Status::invalidArgument("dump packet is empty");
  packets_.push_back(std::move(packet));
  ++packets_written_;
  return framework::Status::success();
}

framework::Status DumpDevice::readPacket(DumpPacket& out) {
  if (!running_) return framework::Status::unavailable("dump device is not running");
  if (packets_.empty()) return framework::Status::unavailable("no dump packet available");
  out = std::move(packets_.front());
  packets_.erase(packets_.begin());
  ++packets_read_;
  return framework::Status::success();
}

framework::Status DumpDevice::stop() {
  if (!open_) return framework::Status::unavailable("dump device is not open");
  running_ = false;
  return framework::Status::success();
}

void DumpDevice::close() {
  open_ = false;
  running_ = false;
  points_.clear();
  packets_.clear();
}

DumpStats DumpDevice::stats() const {
  return {packets_written_, packets_read_, running_};
}

} // namespace audio_studio::drivers::dump
