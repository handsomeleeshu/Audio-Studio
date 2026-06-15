#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::drivers::transport {

using TransportResult = framework::Status;

struct TransportConfig {
  std::string name;
};

struct TransportCaps {
  size_t max_payload_size = 4096;
  bool reliable = true;
  bool ordered = true;
};

class ITransportDriver {
public:
  virtual ~ITransportDriver() = default;

  virtual TransportResult open(const TransportConfig& config) = 0;
  virtual void close() = 0;
  virtual TransportResult write(const uint8_t* data, size_t size, uint32_t timeout_ms) = 0;
  virtual TransportResult read(uint8_t* buffer, size_t capacity, size_t& actual_size, uint32_t timeout_ms) = 0;
  virtual TransportResult flush() = 0;
  virtual bool isConnected() const = 0;
  virtual TransportCaps caps() const = 0;
  virtual std::string name() const = 0;
};

class ITransportDriverFactory {
public:
  virtual ~ITransportDriverFactory() = default;
  virtual std::string name() const = 0;
  virtual std::unique_ptr<ITransportDriver> create(const TransportConfig& config) const = 0;
};

class TransportDriverRegistry {
public:
  TransportResult registerFactory(std::unique_ptr<ITransportDriverFactory> factory) {
    if (!factory) return TransportResult::invalidArgument("transport driver factory is null");
    const auto factory_name = factory->name();
    if (factory_name.empty()) return TransportResult::invalidArgument("transport driver factory name is empty");
    if (factories_.find(factory_name) != factories_.end()) {
      return TransportResult::invalidArgument("transport driver factory already registered: " + factory_name);
    }
    factories_.emplace(factory_name, std::move(factory));
    return TransportResult::success();
  }

  bool hasFactory(const std::string& name) const {
    return factories_.find(name) != factories_.end();
  }

  std::unique_ptr<ITransportDriver> create(const std::string& name, const TransportConfig& config) const {
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
  std::map<std::string, std::unique_ptr<ITransportDriverFactory>> factories_;
};

} // namespace audio_studio::drivers::transport
