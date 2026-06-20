#include "simulator_pipe_datalink_device.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

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
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) return drivers::datalink::DataLinkResult::unavailable("failed to open simulator pipe file: " + path);
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

  if (!loopback_) {
    auto status = ensureFile(rx_path_);
    if (!status.ok()) return status;
    status = ensureFile(tx_path_);
    if (!status.ok()) return status;
  }

  connected_ = true;
  return drivers::datalink::DataLinkResult::success();
}

void SimulatorPipeDataLinkDevice::close() {
  std::lock_guard<std::mutex> lock(mutex_);
  connected_ = false;
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

  std::ofstream output(tx_path_, std::ios::binary | std::ios::app);
  if (!output) return drivers::datalink::DataLinkResult::unavailable("failed to write simulator pipe: " + tx_path_);
  output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
  if (!output) return drivers::datalink::DataLinkResult::unavailable("simulator pipe write failed: " + tx_path_);
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
      } else {
        std::ifstream input(rx_path_, std::ios::binary);
        if (!input) return drivers::datalink::DataLinkResult::unavailable("failed to read simulator pipe: " + rx_path_);
        input.seekg(static_cast<std::streamoff>(read_offset_), std::ios::beg);
        input.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(capacity));
        const auto count = input.gcount();
        if (count > 0) {
          actual_size = static_cast<size_t>(count);
          read_offset_ += actual_size;
          return drivers::datalink::DataLinkResult::success();
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
