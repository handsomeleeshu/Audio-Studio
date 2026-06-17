#pragma once

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "status.hpp"

namespace audio_studio::drivers::dynlib {

using DriverResult = framework::Status;

struct DynlibOpenOptions {
  bool local_symbols = true;
};

class IDynlib {
public:
  virtual ~IDynlib() = default;

  virtual DriverResult open(const std::string& path, const DynlibOpenOptions& options) = 0;
  virtual DriverResult getSymbol(const std::string& name, void** symbol) = 0;
  virtual void close() = 0;
  virtual bool isOpen() const = 0;
  virtual std::string path() const = 0;
};

class IDynlibDriver {
public:
  virtual ~IDynlibDriver() = default;

  virtual std::unique_ptr<IDynlib> createLibrary() = 0;
  virtual std::string platformLibraryExtension() const = 0;
  virtual bool isValidLibraryFile(const std::string& path) const = 0;
};

class IDynlibDriverFactory {
public:
  virtual ~IDynlibDriverFactory() = default;
  virtual std::string name() const = 0;
  virtual std::unique_ptr<IDynlibDriver> create() const = 0;
};

class DynlibDriverRegistry {
public:
  static DynlibDriverRegistry& instance() {
    static DynlibDriverRegistry registry;
    return registry;
  }

  DriverResult registerFactory(std::unique_ptr<IDynlibDriverFactory> factory) {
    if (!factory) return DriverResult::invalidArgument("dynamic library driver factory is null");
    const auto factory_name = factory->name();
    if (factory_name.empty()) return DriverResult::invalidArgument("dynamic library driver factory name is empty");
    if (factories_.find(factory_name) != factories_.end()) {
      return DriverResult::invalidArgument("dynamic library driver factory already registered: " + factory_name);
    }
    factories_.emplace(factory_name, std::move(factory));
    return DriverResult::success();
  }

  bool hasFactory(const std::string& name) const {
    return factories_.find(name) != factories_.end();
  }

  std::unique_ptr<IDynlibDriver> create(const std::string& name) const {
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
  std::map<std::string, std::unique_ptr<IDynlibDriverFactory>> factories_;
};

} // namespace audio_studio::drivers::dynlib
