#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::drivers::pipe {

using DriverResult = framework::Status;

enum class PipeType {
  Fifo,
  NamedPipe,
  Anonymous,
};

struct PipeEndpoint {
  std::string path;
};

struct PipeConfig {
  PipeEndpoint endpoint;
  PipeType type = PipeType::Fifo;
};

class IPipeStream {
public:
  virtual ~IPipeStream() = default;

  virtual DriverResult open(const PipeConfig& config) = 0;
  virtual DriverResult read(void* buffer, size_t capacity, size_t& read_bytes, uint32_t timeout_ms) = 0;
  virtual DriverResult write(const void* data, size_t size, size_t& written_bytes, uint32_t timeout_ms) = 0;
  virtual DriverResult flush() = 0;
  virtual void close() = 0;
  virtual bool isOpen() const = 0;
};

class IPipeDriver {
public:
  virtual ~IPipeDriver() = default;

  virtual std::unique_ptr<IPipeStream> createPipeStream(PipeType type) = 0;
  virtual DriverResult createPipe(const PipeEndpoint& endpoint, PipeType type) = 0;
  virtual DriverResult removePipe(const PipeEndpoint& endpoint, PipeType type) = 0;
  virtual DriverResult exists(const PipeEndpoint& endpoint, bool& result) = 0;
};

class IPipeDriverFactory {
public:
  virtual ~IPipeDriverFactory() = default;
  virtual std::string name() const = 0;
  virtual std::unique_ptr<IPipeDriver> create() const = 0;
};

class PipeDriverRegistry {
public:
  DriverResult registerFactory(std::unique_ptr<IPipeDriverFactory> factory) {
    if (!factory) return DriverResult::invalidArgument("pipe driver factory is null");
    const auto factory_name = factory->name();
    if (factory_name.empty()) return DriverResult::invalidArgument("pipe driver factory name is empty");
    if (factories_.find(factory_name) != factories_.end()) {
      return DriverResult::invalidArgument("pipe driver factory already registered: " + factory_name);
    }
    factories_.emplace(factory_name, std::move(factory));
    return DriverResult::success();
  }

  bool hasFactory(const std::string& name) const {
    return factories_.find(name) != factories_.end();
  }

  std::unique_ptr<IPipeDriver> create(const std::string& name) const {
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
  std::map<std::string, std::unique_ptr<IPipeDriverFactory>> factories_;
};

} // namespace audio_studio::drivers::pipe
