#include "macos_os_driver.hpp"

#include <mach/mach_time.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace audio_studio::drivers::os {

namespace {

class MacOsOsDriverFactory final : public IOsDriverFactory {
public:
  std::string name() const override { return "macos"; }
  std::unique_ptr<IOsDriver> create() const override { return std::make_unique<MacOsOsDriver>(); }
};

const bool kMacOsOsDriverRegistered = [] {
  auto status = OsDriverRegistry::instance().registerFactory(std::make_unique<MacOsOsDriverFactory>());
  (void)status;
  return true;
}();

class MacOsThread final : public IOsThread {
public:
  ~MacOsThread() override {
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

class MacOsMutex final : public IOsMutex {
public:
  void lock() override { mutex_.lock(); }
  bool tryLock() override { return mutex_.try_lock(); }
  void unlock() override { mutex_.unlock(); }

private:
  std::mutex mutex_;
};

class MacOsRecursiveMutex final : public IOsRecursiveMutex {
public:
  void lock() override { mutex_.lock(); }
  bool tryLock() override { return mutex_.try_lock(); }
  void unlock() override { mutex_.unlock(); }

private:
  std::recursive_mutex mutex_;
};

class MacOsEvent final : public IOsEvent {
public:
  OsResult wait(uint32_t timeout_ms) override {
    std::unique_lock<std::mutex> lock(mutex_);
    if (timeout_ms == std::numeric_limits<uint32_t>::max()) {
      cv_.wait(lock, [&] { return signaled_; });
      return OsResult::success();
    }
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

class MacOsSemaphore final : public IOsSemaphore {
public:
  MacOsSemaphore(uint32_t initial_count, uint32_t max_count) : count_(initial_count), max_count_(max_count) {}

  OsResult acquire(uint32_t timeout_ms) override {
    std::unique_lock<std::mutex> lock(mutex_);
    if (timeout_ms == std::numeric_limits<uint32_t>::max()) {
      cv_.wait(lock, [&] { return count_ > 0; });
      --count_;
      return OsResult::success();
    }
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

class MacOsTimer final : public IOsTimer {
public:
  ~MacOsTimer() override {
    stop();
  }

  OsResult startOnce(uint64_t delay_ms) override {
    stop();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      active_ = true;
      expired_ = false;
      cancel_ = false;
    }
    worker_ = std::thread([this, delay_ms] {
      std::unique_lock<std::mutex> lock(mutex_);
      const bool cancelled = cv_.wait_for(lock, std::chrono::milliseconds(delay_ms), [this] { return cancel_; });
      if (!cancelled) {
        expired_ = true;
        active_ = false;
      }
    });
    return OsResult::success();
  }

  void stop() override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      cancel_ = true;
      active_ = false;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

  bool expired() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return expired_;
  }

  bool active() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_;
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::thread worker_;
  bool active_ = false;
  bool expired_ = false;
  bool cancel_ = false;
};

// Mach time conversion utilities (initialized once)
class MachTimeInfo {
public:
  static const MachTimeInfo& instance() {
    static MachTimeInfo info;
    return info;
  }

  uint64_t msToNanos(uint64_t ms) const {
    return ms * nanos_per_ms_;
  }

  uint64_t nanosToMs(uint64_t nanos) const {
    return nanos / nanos_per_ms_;
  }

  uint64_t numer() const { return static_cast<uint64_t>(numer_); }
  uint64_t denom() const { return static_cast<uint64_t>(denom_); }

private:
  MachTimeInfo() {
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    numer_ = timebase.numer;
    denom_ = timebase.denom;
    nanos_per_ms_ = (static_cast<uint64_t>(numer_) * 1000000) / denom_;
  }

  int32_t numer_;
  int32_t denom_;
  uint64_t nanos_per_ms_;
};

} // namespace

std::unique_ptr<IOsThread> MacOsOsDriver::createThread() {
  return std::make_unique<MacOsThread>();
}

std::unique_ptr<IOsMutex> MacOsOsDriver::createMutex() {
  return std::make_unique<MacOsMutex>();
}

std::unique_ptr<IOsRecursiveMutex> MacOsOsDriver::createRecursiveMutex() {
  return std::make_unique<MacOsRecursiveMutex>();
}

std::unique_ptr<IOsEvent> MacOsOsDriver::createEvent() {
  return std::make_unique<MacOsEvent>();
}

std::unique_ptr<IOsSemaphore> MacOsOsDriver::createSemaphore(uint32_t initial_count, uint32_t max_count) {
  if (initial_count > max_count) return nullptr;
  return std::make_unique<MacOsSemaphore>(initial_count, max_count);
}

std::unique_ptr<IOsTimer> MacOsOsDriver::createTimer() {
  return std::make_unique<MacOsTimer>();
}

IOsClock& MacOsOsDriver::clock() {
  return *this;
}

IOsProcess& MacOsOsDriver::process() {
  return *this;
}

IOsSystem& MacOsOsDriver::system() {
  return *this;
}

uint64_t MacOsOsDriver::nowMs() const {
  const auto& time_info = MachTimeInfo::instance();
  const auto mach_nanos = mach_absolute_time();
  return time_info.nanosToMs((mach_nanos * time_info.numer()) / time_info.denom());
}

OsResult MacOsOsDriver::sleepForMs(uint64_t duration_ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
  return OsResult::success();
}

OsResult MacOsOsDriver::setEnv(std::string key, std::string value) {
  if (key.empty()) return OsResult::invalidArgument("env key is empty");
  if (key.find('=') != std::string::npos) return OsResult::invalidArgument("env key contains '='");
  if (::setenv(key.c_str(), value.c_str(), 1) != 0) {
    return OsResult::internal("failed to set env: " + key);
  }
  return OsResult::success();
}

OsResult MacOsOsDriver::getEnv(const std::string& key, std::string& out) const {
  if (key.empty()) return OsResult::invalidArgument("env key is empty");
  const char* value = ::getenv(key.c_str());
  if (value == nullptr) return OsResult::unavailable("env key not found: " + key);
  out = value;
  return OsResult::success();
}

uint64_t MacOsOsDriver::processId() const {
  return static_cast<uint64_t>(::getpid());
}

OsResult MacOsOsDriver::getSystemInfo(OsSystemInfo& out) const {
  // Get macOS version
  char os_release[256];
  size_t size = sizeof(os_release);
  if (::sysctlbyname("kern.osrelease", os_release, &size, nullptr, 0) == 0) {
    char os_type[256];
    size = sizeof(os_type);
    if (::sysctlbyname("kern.ostype", os_type, &size, nullptr, 0) == 0) {
      char machine[256];
      size = sizeof(machine);
      if (::sysctlbyname("hw.machine", machine, &size, nullptr, 0) == 0) {
        out.platform = std::string(os_type) + " " + os_release + " " + machine;
      } else {
        out.platform = std::string(os_type) + " " + os_release;
      }
    } else {
      out.platform = os_release;
    }
  } else {
    out.platform = "macos";
  }

  // Get CPU count
  int cpu_count = 0;
  size = sizeof(cpu_count);
  if (::sysctlbyname("hw.ncpu", &cpu_count, &size, nullptr, 0) == 0) {
    out.cpu_count = static_cast<unsigned>(cpu_count);
  } else {
    const auto hardware_threads = std::thread::hardware_concurrency();
    out.cpu_count = hardware_threads == 0 ? 1 : hardware_threads;
  }

  out.pid = processId();
  return OsResult::success();
}

std::string MacOsOsDriver::temporaryDirectory() const {
  // macOS standard temp directory is usually /tmp or NSTemporaryDirectory
  const char* tmp = ::getenv("TMPDIR");
  return tmp == nullptr ? std::string{"/tmp"} : std::string(tmp);
}

std::string MacOsOsDriver::homeDirectory() const {
  const char* home = ::getenv("HOME");
  return home == nullptr ? std::string{} : std::string(home);
}

} // namespace audio_studio::drivers::os