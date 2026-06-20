#include "simulator_pipe_transport_driver.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

namespace audio_studio::platform::simulator {
namespace {

class SimulatorPipeTransportDriverFactory final : public drivers::transport::ITransportDriverFactory {
public:
  std::string name() const override { return "simulator-pipe"; }
  std::unique_ptr<drivers::transport::ITransportDriver> create(const drivers::transport::TransportConfig& config) const override {
    auto driver = std::make_unique<SimulatorPipeTransportDriver>();
    if (!driver->open(config).ok()) return nullptr;
    return driver;
  }
};

const bool kSimulatorPipeTransportDriverRegistered = [] {
  auto status = drivers::transport::TransportDriverRegistry::instance().registerFactory(
    std::make_unique<SimulatorPipeTransportDriverFactory>());
  (void)status;
  return true;
}();

} // namespace

size_t SimulatorPipeTransportDriver::optionSize(const drivers::transport::TransportConfig& config,
                                                const std::string& key,
                                                size_t fallback) {
  const auto it = config.options.find(key);
  if (it == config.options.end() || it->second.empty()) return fallback;
  return static_cast<size_t>(std::stoul(it->second));
}

std::string SimulatorPipeTransportDriver::optionString(const drivers::transport::TransportConfig& config,
                                                       const std::string& key,
                                                       const std::string& fallback) {
  const auto it = config.options.find(key);
  return it == config.options.end() || it->second.empty() ? fallback : it->second;
}

drivers::transport::TransportResult SimulatorPipeTransportDriver::ensureFile(const std::string& path) const {
  if (path.empty()) return drivers::transport::TransportResult::invalidArgument("simulator pipe path is empty");
  std::error_code error;
  const auto parent = std::filesystem::path(path).parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent, error);
  if (error) return drivers::transport::TransportResult::unavailable("failed to create simulator pipe directory: " + error.message());
  std::ofstream output(path, std::ios::binary | std::ios::app);
  if (!output) return drivers::transport::TransportResult::unavailable("failed to open simulator pipe file: " + path);
  return drivers::transport::TransportResult::success();
}

drivers::transport::TransportResult SimulatorPipeTransportDriver::open(const drivers::transport::TransportConfig& config) {
  std::lock_guard<std::mutex> lock(mutex_);
  name_ = config.name.empty() ? "simulator-pipe" : config.name;
  mtu_ = optionSize(config, "mtu", 512);
  if (mtu_ < 64) return drivers::transport::TransportResult::invalidArgument("simulator pipe MTU is too small");

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
  return drivers::transport::TransportResult::success();
}

void SimulatorPipeTransportDriver::close() {
  std::lock_guard<std::mutex> lock(mutex_);
  connected_ = false;
  loopback_rx_.clear();
}

drivers::transport::TransportResult SimulatorPipeTransportDriver::write(const uint8_t* data, size_t size, uint32_t) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!connected_) return drivers::transport::TransportResult::unavailable("simulator pipe transport is not connected");
  if (data == nullptr && size > 0) return drivers::transport::TransportResult::invalidArgument("simulator pipe write buffer is null");
  if (loopback_) {
    loopback_rx_.insert(loopback_rx_.end(), data, data + size);
    return drivers::transport::TransportResult::success();
  }

  std::ofstream output(tx_path_, std::ios::binary | std::ios::app);
  if (!output) return drivers::transport::TransportResult::unavailable("failed to write simulator pipe: " + tx_path_);
  output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
  if (!output) return drivers::transport::TransportResult::unavailable("simulator pipe write failed: " + tx_path_);
  return drivers::transport::TransportResult::success();
}

drivers::transport::TransportResult SimulatorPipeTransportDriver::read(uint8_t* buffer,
                                                                       size_t capacity,
                                                                       size_t& actual_size,
                                                                       uint32_t timeout_ms) {
  actual_size = 0;
  if (buffer == nullptr && capacity > 0) return drivers::transport::TransportResult::invalidArgument("simulator pipe read buffer is null");
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  while (true) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!connected_) return drivers::transport::TransportResult::unavailable("simulator pipe transport is not connected");
      if (loopback_) {
        const size_t count = std::min(capacity, loopback_rx_.size());
        if (count > 0) {
          std::memcpy(buffer, loopback_rx_.data(), count);
          loopback_rx_.erase(loopback_rx_.begin(), loopback_rx_.begin() + static_cast<long>(count));
          actual_size = count;
          return drivers::transport::TransportResult::success();
        }
      } else {
        std::ifstream input(rx_path_, std::ios::binary);
        if (!input) return drivers::transport::TransportResult::unavailable("failed to read simulator pipe: " + rx_path_);
        input.seekg(static_cast<std::streamoff>(read_offset_), std::ios::beg);
        input.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(capacity));
        const auto count = input.gcount();
        if (count > 0) {
          actual_size = static_cast<size_t>(count);
          read_offset_ += actual_size;
          return drivers::transport::TransportResult::success();
        }
      }
    }

    if (timeout_ms == 0 || std::chrono::steady_clock::now() >= deadline) {
      return drivers::transport::TransportResult::unavailable("simulator pipe read timed out");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

drivers::transport::TransportResult SimulatorPipeTransportDriver::flush() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!connected_) return drivers::transport::TransportResult::unavailable("simulator pipe transport is not connected");
  loopback_rx_.clear();
  return drivers::transport::TransportResult::success();
}

bool SimulatorPipeTransportDriver::isConnected() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return connected_;
}

drivers::transport::TransportCaps SimulatorPipeTransportDriver::caps() const {
  return {mtu_, false, true};
}

std::string SimulatorPipeTransportDriver::name() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return name_;
}

} // namespace audio_studio::platform::simulator
