#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::drivers::socket {

using DriverResult = framework::Status;

enum class SocketType {
  Tcp,
  Udp,
};

struct SocketEndpoint {
  std::string host;
  uint16_t port = 0;
};

struct SocketConfig {
  SocketType type = SocketType::Tcp;
};

class ISocket {
public:
  virtual ~ISocket() = default;

  virtual DriverResult open(const SocketConfig& config) = 0;
  virtual DriverResult bind(const SocketEndpoint& endpoint) = 0;
  virtual DriverResult listen(int backlog) = 0;
  virtual DriverResult accept(std::unique_ptr<ISocket>& client, uint32_t timeout_ms) = 0;
  virtual DriverResult connect(const SocketEndpoint& endpoint, uint32_t timeout_ms) = 0;
  virtual DriverResult send(const uint8_t* data, size_t size, size_t& sent, uint32_t timeout_ms) = 0;
  virtual DriverResult recv(uint8_t* buffer, size_t capacity, size_t& received, uint32_t timeout_ms) = 0;
  virtual DriverResult shutdown() = 0;
  virtual void close() = 0;
  virtual bool isOpen() const = 0;
  virtual bool isConnected() const = 0;
};

class ISocketDriver {
public:
  virtual ~ISocketDriver() = default;
  virtual std::unique_ptr<ISocket> createSocket(SocketType type) = 0;
  virtual DriverResult initialize() = 0;
  virtual void shutdown() = 0;
};

class ISocketDriverFactory {
public:
  virtual ~ISocketDriverFactory() = default;
  virtual std::string name() const = 0;
  virtual std::unique_ptr<ISocketDriver> create() const = 0;
};

class SocketDriverRegistry {
public:
  static SocketDriverRegistry& instance() {
    static SocketDriverRegistry registry;
    return registry;
  }

  DriverResult registerFactory(std::unique_ptr<ISocketDriverFactory> factory) {
    if (!factory) return DriverResult::invalidArgument("socket driver factory is null");
    const auto factory_name = factory->name();
    if (factory_name.empty()) return DriverResult::invalidArgument("socket driver factory name is empty");
    if (factories_.find(factory_name) != factories_.end()) {
      return DriverResult::invalidArgument("socket driver factory already registered: " + factory_name);
    }
    factories_.emplace(factory_name, std::move(factory));
    return DriverResult::success();
  }

  bool hasFactory(const std::string& name) const {
    return factories_.find(name) != factories_.end();
  }

  std::unique_ptr<ISocketDriver> create(const std::string& name) const {
    const auto it = factories_.find(name);
    if (it == factories_.end()) return nullptr;
    return it->second->create();
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
  std::map<std::string, std::unique_ptr<ISocketDriverFactory>> factories_;
};

} // namespace audio_studio::drivers::socket
