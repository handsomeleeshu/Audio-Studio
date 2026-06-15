#include "audio_studio/drivers/control/control_device.hpp"

#include <utility>

namespace audio_studio::drivers::control {

framework::Status ControlDevice::open(std::string profile) {
  if (profile.empty()) return framework::Status::invalidArgument("control profile is empty");
  profile_ = std::move(profile);
  open_ = true;
  return framework::Status::success();
}

framework::Status ControlDevice::setValue(std::string id, std::string value) {
  if (!open_) return framework::Status::unavailable("control device is not open");
  if (id.empty()) return framework::Status::invalidArgument("control id is empty");
  values_[std::move(id)] = std::move(value);
  ++writes_;
  return framework::Status::success();
}

framework::Status ControlDevice::getValue(const std::string& id, std::string& out) {
  if (!open_) return framework::Status::unavailable("control device is not open");
  const auto it = values_.find(id);
  if (it == values_.end()) return framework::Status::unavailable("control value not found: " + id);
  out = it->second;
  ++reads_;
  return framework::Status::success();
}

std::vector<ControlInfo> ControlDevice::listControls() const {
  std::vector<ControlInfo> out;
  out.reserve(values_.size());
  for (const auto& item : values_) out.push_back({item.first, item.first, true, true});
  return out;
}

ControlStats ControlDevice::stats() const {
  return {writes_, reads_, open_};
}

void ControlDevice::close() {
  open_ = false;
}

} // namespace audio_studio::drivers::control
