#pragma once

#include <cstdint>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::drivers::dump {

using DumpResult = framework::Status;

struct DumpDeviceConfig {
  std::string device;
};

struct DumpSessionConfig {
  std::string session_id;
};

struct ProbePoint {
  uint32_t point_id = 0;
  std::string name;
};

struct DumpPointInfo {
  uint32_t point_id = 0;
  std::string name;
};

struct DumpRawPacket {
  uint32_t point_id = 0;
  std::vector<uint8_t> bytes;
};

struct DumpDeviceStats {
  size_t packets_written = 0;
  size_t packets_read = 0;
  bool running = false;
};

class IDumpDevice {
public:
  virtual ~IDumpDevice() = default;

  virtual DumpResult open(const DumpDeviceConfig& config) = 0;
  virtual DumpResult configure(const DumpSessionConfig& config) = 0;
  virtual DumpResult listPoints(std::vector<DumpPointInfo>& points) = 0;
  virtual DumpResult addPoint(const ProbePoint& point) = 0;
  virtual DumpResult removePoint(uint32_t point_id) = 0;
  virtual DumpResult removeAllPoints() = 0;
  virtual DumpResult start() = 0;
  virtual DumpResult stop() = 0;
  virtual DumpResult readPacket(DumpRawPacket& packet, uint32_t timeout_ms) = 0;
  virtual DumpResult getStats(DumpDeviceStats& stats) = 0;
  virtual void close() = 0;
};

class IDumpDeviceFactory {
public:
  virtual ~IDumpDeviceFactory() = default;
  virtual std::string name() const = 0;
  virtual std::unique_ptr<IDumpDevice> create(const DumpDeviceConfig& config) const = 0;
};

class DumpDeviceRegistry {
public:
  static DumpDeviceRegistry& instance() {
    static DumpDeviceRegistry registry;
    return registry;
  }

  DumpResult registerFactory(std::unique_ptr<IDumpDeviceFactory> factory) {
    if (!factory) return DumpResult::invalidArgument("dump device factory is null");
    const auto factory_name = factory->name();
    if (factory_name.empty()) return DumpResult::invalidArgument("dump device factory name is empty");
    if (factories_.find(factory_name) != factories_.end()) {
      return DumpResult::invalidArgument("dump device factory already registered: " + factory_name);
    }
    factories_.emplace(factory_name, std::move(factory));
    return DumpResult::success();
  }

  bool hasFactory(const std::string& name) const {
    return factories_.find(name) != factories_.end();
  }

  std::unique_ptr<IDumpDevice> create(const std::string& name, const DumpDeviceConfig& config) const {
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
  std::map<std::string, std::unique_ptr<IDumpDeviceFactory>> factories_;
};

} // namespace audio_studio::drivers::dump
