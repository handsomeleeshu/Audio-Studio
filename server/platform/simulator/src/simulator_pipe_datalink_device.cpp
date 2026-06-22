#include "simulator_pipe_datalink_device.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cerrno>
#include <filesystem>
#include <memory>
#include <system_error>
#include <thread>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace audio_studio::platform::simulator {
namespace {

class SimulatorPipeDataLinkDeviceFactory final : public drivers::datalink::IDataLinkDeviceFactory {
public:
  std::string name() const override { return "simulator-pipe"; }
  std::unique_ptr<drivers::datalink::IDataLinkDevice> create(const drivers::datalink::DataLinkDeviceConfig& config) const override {
    auto driver = std::make_unique<SimulatorPipeDataLinkDevice>();
    if (!driver->open(config).ok()) return nullptr;
    return driver;
  }
};

const bool kSimulatorPipeDataLinkDeviceRegistered = [] {
  auto status = drivers::datalink::DataLinkDeviceRegistry::instance().registerFactory(
    std::make_unique<SimulatorPipeDataLinkDeviceFactory>());
  (void)status;
  return true;
}();

} // namespace

size_t SimulatorPipeDataLinkDevice::optionSize(const drivers::datalink::DataLinkDeviceConfig& config,
                                               const std::string& key,
                                               size_t fallback) {
  const auto it = config.options.find(key);
  if (it == config.options.end() || it->second.empty()) return fallback;
  return static_cast<size_t>(std::stoul(it->second));
}

std::string SimulatorPipeDataLinkDevice::optionString(const drivers::datalink::DataLinkDeviceConfig& config,
                                                      const std::string& key,
                                                      const std::string& fallback) {
  const auto it = config.options.find(key);
  return it == config.options.end() || it->second.empty() ? fallback : it->second;
}

drivers::datalink::DataLinkResult SimulatorPipeDataLinkDevice::ensureFile(const std::string& path) const {
  if (path.empty()) return drivers::datalink::DataLinkResult::invalidArgument("simulator pipe path is empty");
  std::error_code error;
  const auto parent = std::filesystem::path(path).parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent, error);
  if (error) return drivers::datalink::DataLinkResult::unavailable("failed to create simulator pipe directory: " + error.message());
  struct stat st {};
  if (::lstat(path.c_str(), &st) == 0) {
    if (!S_ISREG(st.st_mode)) {
      std::filesystem::remove(path, error);
      if (error) return drivers::datalink::DataLinkResult::unavailable("failed to replace simulator data-link file: " + error.message());
    }
  }
  const int fd = ::open(path.c_str(), O_CREAT | O_RDWR, 0666);
  if (fd < 0) {
    return drivers::datalink::DataLinkResult::unavailable("failed to create simulator data-link file: " + path);
  }
  ::close(fd);
  return drivers::datalink::DataLinkResult::success();
}

drivers::datalink::DataLinkResult SimulatorPipeDataLinkDevice::open(const drivers::datalink::DataLinkDeviceConfig& config) {
  std::lock_guard<std::mutex> lock(mutex_);
  name_ = config.name.empty() ? "simulator-pipe" : config.name;
  mtu_ = optionSize(config, "mtu", 512);
  if (mtu_ < 64) return drivers::datalink::DataLinkResult::invalidArgument("simulator pipe MTU is too small");

  const std::string endpoint = optionString(config, "endpoint", config.endpoint);
  rx_path_ = optionString(config, "rx_path", endpoint.empty() ? "" : endpoint + ".rx");
  tx_path_ = optionString(config, "tx_path", endpoint.empty() ? "" : endpoint + ".tx");
  loopback_ = rx_path_.empty() && tx_path_.empty();
  read_offset_ = 0;
  loopback_rx_.clear();
  rx_fd_ = -1;
  tx_fd_ = -1;

  if (!loopback_) {
    auto status = ensureFile(rx_path_);
    if (!status.ok()) return status;
    status = ensureFile(tx_path_);
    if (!status.ok()) return status;
    rx_fd_ = ::open(rx_path_.c_str(), O_RDONLY);
    if (rx_fd_ < 0) return drivers::datalink::DataLinkResult::unavailable("failed to open simulator RX data-link file: " + rx_path_);
    const auto rx_end = ::lseek(rx_fd_, 0, SEEK_END);
    if (rx_end < 0) {
      ::close(rx_fd_);
      rx_fd_ = -1;
      return drivers::datalink::DataLinkResult::unavailable("failed to seek simulator RX data-link file: " + rx_path_);
    }
    read_offset_ = static_cast<size_t>(rx_end);
    tx_fd_ = ::open(tx_path_.c_str(), O_WRONLY | O_APPEND);
    if (tx_fd_ < 0) {
      ::close(rx_fd_);
      rx_fd_ = -1;
      return drivers::datalink::DataLinkResult::unavailable("failed to open simulator TX data-link file: " + tx_path_);
    }
  }

  connected_ = true;
  return drivers::datalink::DataLinkResult::success();
}

void SimulatorPipeDataLinkDevice::close() {
  std::lock_guard<std::mutex> lock(mutex_);
  connected_ = false;
  if (rx_fd_ >= 0) {
    ::close(rx_fd_);
    rx_fd_ = -1;
  }
  if (tx_fd_ >= 0) {
    ::close(tx_fd_);
    tx_fd_ = -1;
  }
  loopback_rx_.clear();
}

drivers::datalink::DataLinkResult SimulatorPipeDataLinkDevice::writeBlock(const uint8_t* data, size_t size, uint32_t) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!connected_) return drivers::datalink::DataLinkResult::unavailable("simulator pipe data-link is not connected");
  if (data == nullptr && size > 0) return drivers::datalink::DataLinkResult::invalidArgument("simulator pipe write buffer is null");
  if (loopback_) {
    loopback_rx_.insert(loopback_rx_.end(), data, data + size);
    return drivers::datalink::DataLinkResult::success();
  }

  if (tx_fd_ < 0) return drivers::datalink::DataLinkResult::unavailable("simulator pipe TX data-link file is not open: " + tx_path_);
  const auto written = ::write(tx_fd_, data, size);
  if (written < 0 || static_cast<size_t>(written) != size) {
    return drivers::datalink::DataLinkResult::unavailable("simulator pipe write failed: " + tx_path_);
  }
  (void)::fsync(tx_fd_);
  return drivers::datalink::DataLinkResult::success();
}

drivers::datalink::DataLinkResult SimulatorPipeDataLinkDevice::readBlock(uint8_t* buffer,
                                                                         size_t capacity,
                                                                         size_t& actual_size,
                                                                         uint32_t timeout_ms) {
  actual_size = 0;
  if (buffer == nullptr && capacity > 0) return drivers::datalink::DataLinkResult::invalidArgument("simulator pipe read buffer is null");
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  while (true) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!connected_) return drivers::datalink::DataLinkResult::unavailable("simulator pipe data-link is not connected");
      if (loopback_) {
        const size_t count = std::min(capacity, loopback_rx_.size());
        if (count > 0) {
          std::memcpy(buffer, loopback_rx_.data(), count);
          loopback_rx_.erase(loopback_rx_.begin(), loopback_rx_.begin() + static_cast<long>(count));
          actual_size = count;
          return drivers::datalink::DataLinkResult::success();
        }
      } else if (rx_fd_ >= 0) {
        if (::lseek(rx_fd_, static_cast<off_t>(read_offset_), SEEK_SET) < 0) {
          return drivers::datalink::DataLinkResult::unavailable("simulator pipe seek failed: " + rx_path_);
        }
        const auto count = ::read(rx_fd_, buffer, capacity);
        if (count > 0) {
          actual_size = static_cast<size_t>(count);
          read_offset_ += actual_size;
          return drivers::datalink::DataLinkResult::success();
        }
        if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
          return drivers::datalink::DataLinkResult::unavailable("simulator pipe read failed: " + rx_path_);
        }
      }
    }

    if (timeout_ms == 0 || std::chrono::steady_clock::now() >= deadline) {
      return drivers::datalink::DataLinkResult::unavailable("simulator pipe read timed out");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

drivers::datalink::DataLinkResult SimulatorPipeDataLinkDevice::flush() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!connected_) return drivers::datalink::DataLinkResult::unavailable("simulator pipe data-link is not connected");
  if (tx_fd_ >= 0) (void)::fsync(tx_fd_);
  loopback_rx_.clear();
  return drivers::datalink::DataLinkResult::success();
}

bool SimulatorPipeDataLinkDevice::isConnected() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return connected_;
}

drivers::datalink::DataLinkDeviceCaps SimulatorPipeDataLinkDevice::caps() const {
  return {mtu_, false, true};
}

std::string SimulatorPipeDataLinkDevice::name() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return name_;
}

} // namespace audio_studio::platform::simulator
