#pragma once

#include <cstdint>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "status.hpp"

namespace audio_studio::drivers::log {

using LogResult = framework::Status;

struct LogDeviceConfig {
  std::string source;
};

struct LogRawChunk {
  uint32_t sequence = 0;
  std::vector<uint8_t> bytes;
};

struct LogDeviceStats {
  size_t chunks_written = 0;
  size_t chunks_read = 0;
  bool running = false;
};

class ILogDevice {
public:
  virtual ~ILogDevice() = default;

  virtual LogResult open(const LogDeviceConfig& config) = 0;
  virtual LogResult configure(const LogDeviceConfig& config) = 0;
  virtual LogResult start() = 0;
  virtual LogResult stop() = 0;
  virtual LogResult readChunk(LogRawChunk& chunk, uint32_t timeout_ms) = 0;
  virtual LogResult getStats(LogDeviceStats& stats) = 0;
  virtual void close() = 0;
};

class ILogDeviceFactory {
public:
  virtual ~ILogDeviceFactory() = default;
  virtual std::string name() const = 0;
  virtual std::unique_ptr<ILogDevice> create(const LogDeviceConfig& config) const = 0;
};

class LogDeviceRegistry {
public:
  static LogDeviceRegistry& instance() {
    static LogDeviceRegistry registry;
    return registry;
  }

  LogResult registerFactory(std::unique_ptr<ILogDeviceFactory> factory) {
    if (!factory) return LogResult::invalidArgument("log device factory is null");
    const auto factory_name = factory->name();
    if (factory_name.empty()) return LogResult::invalidArgument("log device factory name is empty");
    if (factories_.find(factory_name) != factories_.end()) {
      return LogResult::invalidArgument("log device factory already registered: " + factory_name);
    }
    factories_.emplace(factory_name, std::move(factory));
    return LogResult::success();
  }

  bool hasFactory(const std::string& name) const {
    return factories_.find(name) != factories_.end();
  }

  std::unique_ptr<ILogDevice> create(const std::string& name, const LogDeviceConfig& config) const {
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
  std::map<std::string, std::unique_ptr<ILogDeviceFactory>> factories_;
};

} // namespace audio_studio::drivers::log
