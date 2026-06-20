#include "macos_datalink_device.hpp"

#include <algorithm>
#include <cstring>

namespace audio_studio::drivers::datalink {

namespace {

class MacOsDataLinkDeviceFactory final : public IDataLinkDeviceFactory {
public:
  std::string name() const override { return "macos"; }
  std::unique_ptr<IDataLinkDevice> create(const DataLinkDeviceConfig& config) const override {
    auto driver = std::make_unique<MacOsDataLinkDevice>();
    if (!driver->open(config).ok()) return nullptr;
    return driver;
  }
};

const bool kMacOsDataLinkDeviceRegistered = [] {
  auto status = DataLinkDeviceRegistry::instance().registerFactory(std::make_unique<MacOsDataLinkDeviceFactory>());
  (void)status;
  return true;
}();

} // namespace

DataLinkResult MacOsDataLinkDevice::open(const DataLinkDeviceConfig& config) {
  if (config.name.empty()) return DataLinkResult::invalidArgument("data-link name is empty");
  name_ = config.name;
  connected_ = true;
  return DataLinkResult::success();
}

void MacOsDataLinkDevice::close() {
  connected_ = false;
  name_.clear();
  rx_buffer_.clear();
}

DataLinkResult MacOsDataLinkDevice::writeBlock(const uint8_t* data, size_t size, uint32_t /*timeout_ms*/) {
  if (!connected_) return DataLinkResult::unavailable("data-link is not connected");
  if (data == nullptr && size > 0) return DataLinkResult::invalidArgument("data-link write buffer is null");
  rx_buffer_.insert(rx_buffer_.end(), data, data + size);
  bytes_written_ += size;
  return DataLinkResult::success();
}

DataLinkResult MacOsDataLinkDevice::readBlock(uint8_t* buffer, size_t capacity, size_t& actual_size, uint32_t /*timeout_ms*/) {
  if (!connected_) return DataLinkResult::unavailable("data-link is not connected");
  if (buffer == nullptr && capacity > 0) return DataLinkResult::invalidArgument("data-link read buffer is null");
  const auto count = std::min(capacity, rx_buffer_.size());
  if (count > 0) std::memcpy(buffer, rx_buffer_.data(), count);
  rx_buffer_.erase(rx_buffer_.begin(), rx_buffer_.begin() + count);
  bytes_read_ += count;
  actual_size = count;
  return DataLinkResult::success();
}

DataLinkResult MacOsDataLinkDevice::flush() {
  if (!connected_) return DataLinkResult::unavailable("data-link is not connected");
  rx_buffer_.clear();
  return DataLinkResult::success();
}

bool MacOsDataLinkDevice::isConnected() const {
  return connected_;
}

DataLinkDeviceCaps MacOsDataLinkDevice::caps() const {
  return {};
}

std::string MacOsDataLinkDevice::name() const {
  return name_;
}

size_t MacOsDataLinkDevice::bytesWritten() const {
  return bytes_written_;
}

size_t MacOsDataLinkDevice::bytesRead() const {
  return bytes_read_;
}

} // namespace audio_studio::drivers::datalink
