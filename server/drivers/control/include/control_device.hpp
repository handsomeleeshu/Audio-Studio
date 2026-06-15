#pragma once

#include <cstdint>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::drivers::control {

using ControlId = std::string;
using ControlResult = framework::Status;

struct ControlSelector {
  std::string profile;
  std::string device;
  std::string name_or_id;
};

enum class ControlValueType {
  Bool,
  Int,
  Int64,
  Float,
  Enum,
  Bytes,
  String,
};

struct ControlEnumItem {
  std::string label;
  int64_t value = 0;
};

struct ControlRange {
  int64_t min = 0;
  int64_t max = 0;
  int64_t step = 1;
};

struct ControlInfo {
  ControlId id;
  std::string name;
  ControlValueType type = ControlValueType::String;
  bool readable = true;
  bool writable = true;
  bool runtime_settable = true;
  ControlRange range;
  std::vector<ControlEnumItem> enum_items;
  std::string unit;
  std::string owner;
  uint32_t flags = 0;
};

struct ControlValue {
  ControlValueType type = ControlValueType::String;
  bool bool_value = false;
  int64_t int_value = 0;
  double float_value = 0.0;
  std::vector<uint8_t> bytes;
  std::string text;
};

struct ControlDeviceConfig {
  std::string profile;
  std::string device;
};

struct ControlDeviceStats {
  size_t writes = 0;
  size_t reads = 0;
  bool open = false;
};

class IControlDevice {
public:
  virtual ~IControlDevice() = default;

  virtual ControlResult open(const ControlDeviceConfig& config) = 0;
  virtual ControlResult listControls(std::vector<ControlInfo>& controls) = 0;
  virtual ControlResult getControlInfo(const ControlId& id, ControlInfo& info) = 0;
  virtual ControlResult getValue(const ControlId& id, ControlValue& value, uint32_t timeout_ms) = 0;
  virtual ControlResult setValue(const ControlId& id, const ControlValue& value, uint32_t timeout_ms) = 0;
  virtual ControlResult getStats(ControlDeviceStats& stats) = 0;
  virtual void close() = 0;
};

class IControlDeviceFactory {
public:
  virtual ~IControlDeviceFactory() = default;
  virtual std::string name() const = 0;
  virtual std::unique_ptr<IControlDevice> create(const ControlDeviceConfig& config) const = 0;
};

class ControlDeviceRegistry {
public:
  ControlResult registerFactory(std::unique_ptr<IControlDeviceFactory> factory) {
    if (!factory) return ControlResult::invalidArgument("control device factory is null");
    const auto factory_name = factory->name();
    if (factory_name.empty()) return ControlResult::invalidArgument("control device factory name is empty");
    if (factories_.find(factory_name) != factories_.end()) {
      return ControlResult::invalidArgument("control device factory already registered: " + factory_name);
    }
    factories_.emplace(factory_name, std::move(factory));
    return ControlResult::success();
  }

  bool hasFactory(const std::string& name) const {
    return factories_.find(name) != factories_.end();
  }

  std::unique_ptr<IControlDevice> create(const std::string& name, const ControlDeviceConfig& config) const {
    const auto it = factories_.find(name);
    if (it == factories_.end()) return nullptr;
    return it->second->create(config);
  }

  std::vector<std::string> factoryNames() const {
    std::vector<std::string> names;
    names.reserve(factories_.size());
    for (const auto& item : factories_) names.push_back(item.first);
    return names;
  }

  void clear() {
    factories_.clear();
  }

private:
  std::map<std::string, std::unique_ptr<IControlDeviceFactory>> factories_;
};

} // namespace audio_studio::drivers::control
