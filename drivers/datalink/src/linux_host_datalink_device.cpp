#include "linux_host_datalink_device.hpp"

#include <algorithm>
#include <cstring>

namespace audio_studio::drivers::datalink {

namespace {

class LinuxHostDataLinkDeviceFactory final : public IDataLinkDeviceFactory {
public:
  std::string name() const override { return "linux-host"; }
  std::unique_ptr<IDataLinkDevice> create(const DataLinkDeviceConfig& config) const override {
    auto driver = std::make_unique<LinuxHostDataLinkDevice>();
    if (!driver->open(config).ok()) return nullptr;
    return driver;
  }
};

const bool kLinuxHostDataLinkDeviceRegistered = [] {
  auto status = DataLinkDeviceRegistry::instance().registerFactory(std::make_unique<LinuxHostDataLinkDeviceFactory>());
  (void)status;
  return true;
}();

} // namespace

DataLinkResult LinuxHostDataLinkDevice::open(const DataLinkDeviceConfig& config) {
  if (config.name.empty()) return DataLinkResult::invalidArgument("data-link name is empty");
  name_ = config.name;
  connected_ = true;
  return DataLinkResult::success();
}

void LinuxHostDataLinkDevice::close() {
  connected_ = false;
  name_.clear();
  rx_buffer_.clear();
}

DataLinkResult LinuxHostDataLinkDevice::writeBlock(const uint8_t* data, size_t size, uint32_t /*timeout_ms*/) {
  if (!connected_) return DataLinkResult::unavailable("data-link is not connected");
  if (data == nullptr && size > 0) return DataLinkResult::invalidArgument("data-link write buffer is null");
  rx_buffer_.insert(rx_buffer_.end(), data, data + size);
  bytes_written_ += size;
  return DataLinkResult::success();
}

DataLinkResult LinuxHostDataLinkDevice::readBlock(uint8_t* buffer, size_t capacity, size_t& actual_size, uint32_t /*timeout_ms*/) {
  if (!connected_) return DataLinkResult::unavailable("data-link is not connected");
  if (buffer == nullptr && capacity > 0) return DataLinkResult::invalidArgument("data-link read buffer is null");
  const auto count = std::min(capacity, rx_buffer_.size());
  if (count > 0) std::memcpy(buffer, rx_buffer_.data(), count);
  rx_buffer_.erase(rx_buffer_.begin(), rx_buffer_.begin() + count);
  bytes_read_ += count;
  actual_size = count;
  return DataLinkResult::success();
}

DataLinkResult LinuxHostDataLinkDevice::flush() {
  if (!connected_) return DataLinkResult::unavailable("data-link is not connected");
  rx_buffer_.clear();
  return DataLinkResult::success();
}

bool LinuxHostDataLinkDevice::isConnected() const {
  return connected_;
}

DataLinkDeviceCaps LinuxHostDataLinkDevice::caps() const {
  return {};
}

std::string LinuxHostDataLinkDevice::name() const {
  return name_;
}

size_t LinuxHostDataLinkDevice::bytesWritten() const {
  return bytes_written_;
}

size_t LinuxHostDataLinkDevice::bytesRead() const {
  return bytes_read_;
}

} // namespace audio_studio::drivers::datalink
