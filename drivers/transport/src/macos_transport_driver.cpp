#include "macos_transport_driver.hpp"

#include <algorithm>
#include <cstring>

namespace audio_studio::drivers::transport {

namespace {

class MacOsTransportDriverFactory final : public ITransportDriverFactory {
public:
  std::string name() const override { return "macos"; }
  std::unique_ptr<ITransportDriver> create(const TransportConfig& config) const override {
    auto driver = std::make_unique<MacOsTransportDriver>();
    if (!driver->open(config).ok()) return nullptr;
    return driver;
  }
};

const bool kMacOsTransportDriverRegistered = [] {
  auto status = TransportDriverRegistry::instance().registerFactory(std::make_unique<MacOsTransportDriverFactory>());
  (void)status;
  return true;
}();

} // namespace

TransportResult MacOsTransportDriver::open(const TransportConfig& config) {
  if (config.name.empty()) return TransportResult::invalidArgument("transport name is empty");
  name_ = config.name;
  connected_ = true;
  return TransportResult::success();
}

void MacOsTransportDriver::close() {
  connected_ = false;
  name_.clear();
  rx_buffer_.clear();
}

TransportResult MacOsTransportDriver::write(const uint8_t* data, size_t size, uint32_t /*timeout_ms*/) {
  if (!connected_) return TransportResult::unavailable("transport is not connected");
  if (data == nullptr && size > 0) return TransportResult::invalidArgument("transport write buffer is null");
  rx_buffer_.insert(rx_buffer_.end(), data, data + size);
  bytes_written_ += size;
  return TransportResult::success();
}

TransportResult MacOsTransportDriver::read(uint8_t* buffer, size_t capacity, size_t& actual_size, uint32_t /*timeout_ms*/) {
  if (!connected_) return TransportResult::unavailable("transport is not connected");
  if (buffer == nullptr && capacity > 0) return TransportResult::invalidArgument("transport read buffer is null");
  const auto count = std::min(capacity, rx_buffer_.size());
  if (count > 0) std::memcpy(buffer, rx_buffer_.data(), count);
  rx_buffer_.erase(rx_buffer_.begin(), rx_buffer_.begin() + count);
  bytes_read_ += count;
  actual_size = count;
  return TransportResult::success();
}

TransportResult MacOsTransportDriver::flush() {
  if (!connected_) return TransportResult::unavailable("transport is not connected");
  rx_buffer_.clear();
  return TransportResult::success();
}

bool MacOsTransportDriver::isConnected() const {
  return connected_;
}

TransportCaps MacOsTransportDriver::caps() const {
  return {};
}

std::string MacOsTransportDriver::name() const {
  return name_;
}

size_t MacOsTransportDriver::bytesWritten() const {
  return bytes_written_;
}

size_t MacOsTransportDriver::bytesRead() const {
  return bytes_read_;
}

} // namespace audio_studio::drivers::transport
