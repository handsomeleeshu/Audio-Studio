#pragma once

#include <map>

#include "control_device.hpp"

namespace audio_studio::drivers::control {

class LinuxHostControlDevice final : public IControlDevice {
public:
  ControlResult open(const ControlDeviceConfig& config) override;
  ControlResult listControls(std::vector<ControlInfo>& controls) override;
  ControlResult getControlInfo(const ControlId& id, ControlInfo& info) override;
  ControlResult getValue(const ControlId& id, ControlValue& value, uint32_t timeout_ms) override;
  ControlResult setValue(const ControlId& id, const ControlValue& value, uint32_t timeout_ms) override;
  ControlResult getStats(ControlDeviceStats& stats) override;
  void close() override;

private:
  ControlDeviceConfig config_;
  bool open_ = false;
  size_t writes_ = 0;
  size_t reads_ = 0;
  std::map<ControlId, ControlInfo> infos_;
  std::map<ControlId, ControlValue> values_;
};

} // namespace audio_studio::drivers::control
