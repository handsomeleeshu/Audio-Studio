#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "status.hpp"

namespace audio_studio::drivers::os {

using OsResult = framework::Status;
using OsThreadMain = std::function<void()>;

struct OsSystemInfo {
  std::string platform;
  unsigned cpu_count = 1;
  uint64_t pid = 1;
};

class IOsThread {
public:
  virtual ~IOsThread() = default;
  virtual OsResult start(std::string name, OsThreadMain main) = 0;
  virtual OsResult join() = 0;
  virtual bool joinable() const = 0;
  virtual std::string name() const = 0;
};

class IOsMutex {
public:
  virtual ~IOsMutex() = default;
  virtual void lock() = 0;
  virtual bool tryLock() = 0;
  virtual void unlock() = 0;
};

class IOsRecursiveMutex {
public:
  virtual ~IOsRecursiveMutex() = default;
  virtual void lock() = 0;
  virtual bool tryLock() = 0;
  virtual void unlock() = 0;
};

class IOsEvent {
public:
  virtual ~IOsEvent() = default;
  virtual OsResult wait(uint32_t timeout_ms) = 0;
  virtual void signal() = 0;
  virtual void reset() = 0;
  virtual bool isSignaled() const = 0;
};

class IOsSemaphore {
public:
  virtual ~IOsSemaphore() = default;
  virtual OsResult acquire(uint32_t timeout_ms) = 0;
  virtual OsResult release(uint32_t count) = 0;
  virtual uint32_t count() const = 0;
};

class IOsTimer {
public:
  virtual ~IOsTimer() = default;
  virtual OsResult startOnce(uint64_t delay_ms) = 0;
  virtual void stop() = 0;
  virtual bool expired() const = 0;
  virtual bool active() const = 0;
};

class IOsClock {
public:
  virtual ~IOsClock() = default;
  virtual uint64_t nowMs() const = 0;
  virtual OsResult sleepForMs(uint64_t duration_ms) = 0;
};

class IOsProcess {
public:
  virtual ~IOsProcess() = default;
  virtual OsResult setEnv(std::string key, std::string value) = 0;
  virtual OsResult getEnv(const std::string& key, std::string& out) const = 0;
  virtual uint64_t processId() const = 0;
  virtual std::string executablePath() const = 0;
  virtual OsResult runCommand(const std::string& command, int& exit_code) = 0;
};

class IOsSystem {
public:
  virtual ~IOsSystem() = default;
  virtual OsResult getSystemInfo(OsSystemInfo& out) const = 0;
  virtual std::string temporaryDirectory() const = 0;
  virtual std::string homeDirectory() const = 0;
};

class IOsDriver {
public:
  virtual ~IOsDriver() = default;

  virtual std::unique_ptr<IOsThread> createThread() = 0;
  virtual std::unique_ptr<IOsMutex> createMutex() = 0;
  virtual std::unique_ptr<IOsRecursiveMutex> createRecursiveMutex() = 0;
  virtual std::unique_ptr<IOsEvent> createEvent() = 0;
  virtual std::unique_ptr<IOsSemaphore> createSemaphore(uint32_t initial_count, uint32_t max_count) = 0;
  virtual std::unique_ptr<IOsTimer> createTimer() = 0;

  virtual IOsClock& clock() = 0;
  virtual IOsProcess& process() = 0;
  virtual IOsSystem& system() = 0;
};

class IOsDriverFactory {
public:
  virtual ~IOsDriverFactory() = default;
  virtual std::string name() const = 0;
  virtual std::unique_ptr<IOsDriver> create() const = 0;
};

class OsDriverRegistry {
public:
  static OsDriverRegistry& instance() {
    static OsDriverRegistry registry;
    return registry;
  }

  OsResult registerFactory(std::unique_ptr<IOsDriverFactory> factory) {
    if (!factory) return OsResult::invalidArgument("OS driver factory is null");
    const auto factory_name = factory->name();
    if (factory_name.empty()) return OsResult::invalidArgument("OS driver factory name is empty");
    if (factories_.find(factory_name) != factories_.end()) {
      return OsResult::invalidArgument("OS driver factory already registered: " + factory_name);
    }
    factories_.emplace(factory_name, std::move(factory));
    return OsResult::success();
  }

  bool hasFactory(const std::string& name) const {
    return factories_.find(name) != factories_.end();
  }

  std::unique_ptr<IOsDriver> create(const std::string& name) const {
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
  std::map<std::string, std::unique_ptr<IOsDriverFactory>> factories_;
};

} // namespace audio_studio::drivers::os
