#include "linux_host_os_driver.hpp"

#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace audio_studio::drivers::os {

namespace {

class LinuxHostOsDriverFactory final : public IOsDriverFactory {
public:
  std::string name() const override { return "linux-host"; }
  std::unique_ptr<IOsDriver> create() const override { return std::make_unique<LinuxHostOsDriver>(); }
};

const bool kLinuxHostOsDriverRegistered = [] {
  auto status = OsDriverRegistry::instance().registerFactory(std::make_unique<LinuxHostOsDriverFactory>());
  (void)status;
  return true;
}();

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

class LinuxHostSemaphore final : public IOsSemaphore {
public:
  LinuxHostSemaphore(uint32_t initial_count, uint32_t max_count) : count_(initial_count), max_count_(max_count) {}

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

class LinuxHostTimer final : public IOsTimer {
public:
  ~LinuxHostTimer() override {
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
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

OsResult LinuxHostOsDriver::sleepForMs(uint64_t duration_ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
  return OsResult::success();
}

OsResult LinuxHostOsDriver::setEnv(std::string key, std::string value) {
  if (key.empty()) return OsResult::invalidArgument("env key is empty");
  if (key.find('=') != std::string::npos) return OsResult::invalidArgument("env key contains '='");
  if (::setenv(key.c_str(), value.c_str(), 1) != 0) return OsResult::internal("failed to set env: " + key);
  return OsResult::success();
}

OsResult LinuxHostOsDriver::getEnv(const std::string& key, std::string& out) const {
  if (key.empty()) return OsResult::invalidArgument("env key is empty");
  const char* value = ::getenv(key.c_str());
  if (value == nullptr) return OsResult::unavailable("env key not found: " + key);
  out = value;
  return OsResult::success();
}

uint64_t LinuxHostOsDriver::processId() const {
  return static_cast<uint64_t>(::getpid());
}

OsResult LinuxHostOsDriver::runCommand(const std::string& command, int& exit_code) {
  exit_code = -1;
  if (command.empty()) return OsResult::invalidArgument("command is empty");
  const int status = std::system(command.c_str());
  if (status == -1) return OsResult::internal("failed to run command: " + command);
  if (WIFEXITED(status)) {
    exit_code = WEXITSTATUS(status);
    return OsResult::success();
  }
  if (WIFSIGNALED(status)) {
    exit_code = 128 + WTERMSIG(status);
    return OsResult::success();
  }
  return OsResult::internal("command ended in an unknown state: " + command);
}

OsResult LinuxHostOsDriver::getSystemInfo(OsSystemInfo& out) const {
  struct utsname name {};
  if (::uname(&name) == 0) {
    out.platform = std::string(name.sysname) + " " + name.release + " " + name.machine;
  } else {
    out.platform = "linux-host";
  }
  const auto hardware_threads = std::thread::hardware_concurrency();
  out.cpu_count = hardware_threads == 0 ? 1 : hardware_threads;
  out.pid = processId();
  return OsResult::success();
}

std::string LinuxHostOsDriver::temporaryDirectory() const {
  std::error_code ec;
  const auto path = std::filesystem::temp_directory_path(ec);
  return ec ? "/tmp" : path.string();
}

std::string LinuxHostOsDriver::homeDirectory() const {
  const char* home = ::getenv("HOME");
  return home == nullptr ? std::string{} : std::string(home);
}

} // namespace audio_studio::drivers::os
