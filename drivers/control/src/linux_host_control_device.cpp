#include "linux_host_control_device.hpp"

#include <utility>

namespace audio_studio::drivers::control {

namespace {

class LinuxHostControlDeviceFactory final : public IControlDeviceFactory {
public:
  std::string name() const override { return "linux-host"; }
  std::unique_ptr<IControlDevice> create(const ControlDeviceConfig& config) const override {
    auto device = std::make_unique<LinuxHostControlDevice>();
    if (!device->open(config).ok()) return nullptr;
    return device;
  }
};

const bool kLinuxHostControlDeviceRegistered = [] {
  auto status = ControlDeviceRegistry::instance().registerFactory(std::make_unique<LinuxHostControlDeviceFactory>());
  (void)status;
  return true;
}();

} // namespace

ControlResult LinuxHostControlDevice::open(const ControlDeviceConfig& config) {
  if (config.profile.empty()) return ControlResult::invalidArgument("control profile is empty");
  config_ = config;
  open_ = true;
  return ControlResult::success();
}

ControlResult LinuxHostControlDevice::listControls(std::vector<ControlInfo>& controls) {
  if (!open_) return ControlResult::unavailable("control device is not open");
  controls.clear();
  for (const auto& item : infos_) controls.push_back(item.second);
  return ControlResult::success();
}

ControlResult LinuxHostControlDevice::getControlInfo(const ControlId& id, ControlInfo& info) {
  if (!open_) return ControlResult::unavailable("control device is not open");
  const auto it = infos_.find(id);
  if (it == infos_.end()) return ControlResult::unavailable("control info not found: " + id);
  info = it->second;
  return ControlResult::success();
}

ControlResult LinuxHostControlDevice::getValue(const ControlId& id, ControlValue& value, uint32_t /*timeout_ms*/) {
  if (!open_) return ControlResult::unavailable("control device is not open");
  const auto it = values_.find(id);
  if (it == values_.end()) return ControlResult::unavailable("control value not found: " + id);
  value = it->second;
  ++reads_;
  return ControlResult::success();
}

ControlResult LinuxHostControlDevice::setValue(const ControlId& id, const ControlValue& value, uint32_t /*timeout_ms*/) {
  if (!open_) return ControlResult::unavailable("control device is not open");
  if (id.empty()) return ControlResult::invalidArgument("control id is empty");
  values_[id] = value;
  if (infos_.find(id) == infos_.end()) {
    infos_[id] = ControlInfo{id, id, value.type, true, true, true, {}, {}, "", config_.device, 0};
  }
  ++writes_;
  return ControlResult::success();
}

ControlResult LinuxHostControlDevice::getStats(ControlDeviceStats& stats) {
  stats = {writes_, reads_, open_};
  return ControlResult::success();
}

void LinuxHostControlDevice::close() {
  open_ = false;
}

} // namespace audio_studio::drivers::control
