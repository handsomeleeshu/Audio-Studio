#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "status.hpp"

namespace audio_studio::drivers::datalink {

using DataLinkResult = framework::Status;

struct DataLinkDeviceConfig {
  std::string name;
  std::string endpoint;
  std::map<std::string, std::string> options;
};

struct DataLinkDeviceCaps {
  size_t max_payload_size = 4096;
  bool reliable = true;
  bool ordered = true;
};

class IDataLinkDevice {
public:
  virtual ~IDataLinkDevice() = default;

  virtual DataLinkResult open(const DataLinkDeviceConfig& config) = 0;
  virtual void close() = 0;
  virtual DataLinkResult writeBlock(const uint8_t* data, size_t size, uint32_t timeout_ms) = 0;
  virtual DataLinkResult readBlock(uint8_t* buffer, size_t capacity, size_t& actual_size, uint32_t timeout_ms) = 0;
  virtual DataLinkResult flush() = 0;
  virtual bool isConnected() const = 0;
  virtual DataLinkDeviceCaps caps() const = 0;
  virtual std::string name() const = 0;
};

class IDataLinkDeviceFactory {
public:
  virtual ~IDataLinkDeviceFactory() = default;
  virtual std::string name() const = 0;
  virtual std::unique_ptr<IDataLinkDevice> create(const DataLinkDeviceConfig& config) const = 0;
};

class DataLinkDeviceRegistry {
public:
  static DataLinkDeviceRegistry& instance() {
    static DataLinkDeviceRegistry registry;
    return registry;
  }

  DataLinkResult registerFactory(std::unique_ptr<IDataLinkDeviceFactory> factory) {
    if (!factory) return DataLinkResult::invalidArgument("data-link device factory is null");
    const auto factory_name = factory->name();
    if (factory_name.empty()) return DataLinkResult::invalidArgument("data-link device factory name is empty");
    if (factories_.find(factory_name) != factories_.end()) {
      return DataLinkResult::invalidArgument("data-link device factory already registered: " + factory_name);
    }
    factories_.emplace(factory_name, std::move(factory));
    return DataLinkResult::success();
  }

  bool hasFactory(const std::string& name) const {
    return factories_.find(name) != factories_.end();
  }

  std::unique_ptr<IDataLinkDevice> create(const std::string& name, const DataLinkDeviceConfig& config) const {
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
  std::map<std::string, std::unique_ptr<IDataLinkDeviceFactory>> factories_;
};

} // namespace audio_studio::drivers::datalink
