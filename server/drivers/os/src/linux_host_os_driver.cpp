#include "linux_host_os_driver.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace audio_studio::drivers::os {

namespace {

class LinuxHostThread final : public IOsThread {
public:
  ~LinuxHostThread() override {
    if (thread_.joinable()) thread_.join();
  }

  OsResult start(std::string name, OsThreadMain main) override {
    if (thread_.joinable()) return OsResult::unavailable("thread already started");
    if (!main) return OsResult::invalidArgument("thread main is empty");
    name_ = std::move(name);
    thread_ = std::thread(std::move(main));
    return OsResult::success();
  }

  OsResult join() override {
    if (!thread_.joinable()) return OsResult::unavailable("thread is not joinable");
    thread_.join();
    return OsResult::success();
  }

  bool joinable() const override {
    return thread_.joinable();
  }

  std::string name() const override {
    return name_;
  }

private:
  std::string name_;
  std::thread thread_;
};

class LinuxHostMutex final : public IOsMutex {
public:
  void lock() override { mutex_.lock(); }
  bool tryLock() override { return mutex_.try_lock(); }
  void unlock() override { mutex_.unlock(); }

private:
  std::mutex mutex_;
};

class LinuxHostRecursiveMutex final : public IOsRecursiveMutex {
public:
  void lock() override { mutex_.lock(); }
  bool tryLock() override { return mutex_.try_lock(); }
  void unlock() override { mutex_.unlock(); }

private:
  std::recursive_mutex mutex_;
};

class LinuxHostEvent final : public IOsEvent {
public:
  OsResult wait(uint32_t timeout_ms) override {
    std::unique_lock<std::mutex> lock(mutex_);
    const auto signaled = cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] { return signaled_; });
    if (!signaled) return OsResult::unavailable("event wait timed out");
    return OsResult::success();
  }

  void signal() override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      signaled_ = true;
    }
    cv_.notify_all();
  }

  void reset() override {
    std::lock_guard<std::mutex> lock(mutex_);
    signaled_ = false;
  }

  bool isSignaled() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return signaled_;
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool signaled_ = false;
};

class LinuxHostSemaphore final : public IOsSemaphore {
public:
  LinuxHostSemaphore(uint32_t initial_count, uint32_t max_count) : count_(initial_count), max_count_(max_count) {}

  OsResult acquire(uint32_t timeout_ms) override {
    std::unique_lock<std::mutex> lock(mutex_);
    const auto available = cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] { return count_ > 0; });
    if (!available) return OsResult::unavailable("semaphore wait timed out");
    --count_;
    return OsResult::success();
  }

  OsResult release(uint32_t count) override {
    if (count == 0) return OsResult::invalidArgument("semaphore release count is zero");
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (count_ + count > max_count_) return OsResult::invalidArgument("semaphore release exceeds max count");
      count_ += count;
    }
    cv_.notify_all();
    return OsResult::success();
  }

  uint32_t count() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  uint32_t count_ = 0;
  uint32_t max_count_ = 0;
};

class LinuxHostTimer final : public IOsTimer {
public:
  OsResult startOnce(uint64_t delay_ms) override {
    active_ = true;
    expired_ = delay_ms == 0;
    delay_ms_ = delay_ms;
    return OsResult::success();
  }

  void stop() override {
    active_ = false;
    expired_ = false;
  }

  bool expired() const override {
    return expired_;
  }

  bool active() const override {
    return active_;
  }

private:
  uint64_t delay_ms_ = 0;
  bool active_ = false;
  bool expired_ = false;
};

} // namespace

std::unique_ptr<IOsThread> LinuxHostOsDriver::createThread() {
  return std::make_unique<LinuxHostThread>();
}

std::unique_ptr<IOsMutex> LinuxHostOsDriver::createMutex() {
  return std::make_unique<LinuxHostMutex>();
}

std::unique_ptr<IOsRecursiveMutex> LinuxHostOsDriver::createRecursiveMutex() {
  return std::make_unique<LinuxHostRecursiveMutex>();
}

std::unique_ptr<IOsEvent> LinuxHostOsDriver::createEvent() {
  return std::make_unique<LinuxHostEvent>();
}

std::unique_ptr<IOsSemaphore> LinuxHostOsDriver::createSemaphore(uint32_t initial_count, uint32_t max_count) {
  if (initial_count > max_count) return nullptr;
  return std::make_unique<LinuxHostSemaphore>(initial_count, max_count);
}

std::unique_ptr<IOsTimer> LinuxHostOsDriver::createTimer() {
  return std::make_unique<LinuxHostTimer>();
}

IOsClock& LinuxHostOsDriver::clock() {
  return *this;
}

IOsProcess& LinuxHostOsDriver::process() {
  return *this;
}

IOsSystem& LinuxHostOsDriver::system() {
  return *this;
}

uint64_t LinuxHostOsDriver::nowMs() const {
  return monotonic_ms_;
}

OsResult LinuxHostOsDriver::sleepForMs(uint64_t duration_ms) {
  monotonic_ms_ += duration_ms;
  return OsResult::success();
}

OsResult LinuxHostOsDriver::setEnv(std::string key, std::string value) {
  if (key.empty()) return OsResult::invalidArgument("env key is empty");
  env_[std::move(key)] = std::move(value);
  return OsResult::success();
}

OsResult LinuxHostOsDriver::getEnv(const std::string& key, std::string& out) const {
  const auto it = env_.find(key);
  if (it == env_.end()) return OsResult::unavailable("env key not found: " + key);
  out = it->second;
  return OsResult::success();
}

uint64_t LinuxHostOsDriver::processId() const {
  return 1;
}

OsResult LinuxHostOsDriver::getSystemInfo(OsSystemInfo& out) const {
  out.platform = "linux-host-test";
  const auto hardware_threads = std::thread::hardware_concurrency();
  out.cpu_count = hardware_threads == 0 ? 1 : hardware_threads;
  out.pid = processId();
  return OsResult::success();
}

std::string LinuxHostOsDriver::temporaryDirectory() const {
  return "/tmp";
}

std::string LinuxHostOsDriver::homeDirectory() const {
  return "/home/audio-studio";
}

} // namespace audio_studio::drivers::os
