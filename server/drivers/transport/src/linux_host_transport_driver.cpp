#include "linux_host_transport_driver.hpp"

#include <algorithm>
#include <cstring>

namespace audio_studio::drivers::transport {

TransportResult LinuxHostTransportDriver::open(const TransportConfig& config) {
  if (config.name.empty()) return TransportResult::invalidArgument("transport name is empty");
  name_ = config.name;
  connected_ = true;
  return TransportResult::success();
}

void LinuxHostTransportDriver::close() {
  connected_ = false;
  name_.clear();
  rx_buffer_.clear();
}

TransportResult LinuxHostTransportDriver::write(const uint8_t* data, size_t size, uint32_t /*timeout_ms*/) {
  if (!connected_) return TransportResult::unavailable("transport is not connected");
  if (data == nullptr && size > 0) return TransportResult::invalidArgument("transport write buffer is null");
  rx_buffer_.insert(rx_buffer_.end(), data, data + size);
  bytes_written_ += size;
  return TransportResult::success();
}

TransportResult LinuxHostTransportDriver::read(uint8_t* buffer, size_t capacity, size_t& actual_size, uint32_t /*timeout_ms*/) {
  if (!connected_) return TransportResult::unavailable("transport is not connected");
  if (buffer == nullptr && capacity > 0) return TransportResult::invalidArgument("transport read buffer is null");
  const auto count = std::min(capacity, rx_buffer_.size());
  if (count > 0) std::memcpy(buffer, rx_buffer_.data(), count);
  rx_buffer_.erase(rx_buffer_.begin(), rx_buffer_.begin() + count);
  bytes_read_ += count;
  actual_size = count;
  return TransportResult::success();
}

TransportResult LinuxHostTransportDriver::flush() {
  if (!connected_) return TransportResult::unavailable("transport is not connected");
  rx_buffer_.clear();
  return TransportResult::success();
}

bool LinuxHostTransportDriver::isConnected() const {
  return connected_;
}

TransportCaps LinuxHostTransportDriver::caps() const {
  return {};
}

std::string LinuxHostTransportDriver::name() const {
  return name_;
}

size_t LinuxHostTransportDriver::bytesWritten() const {
  return bytes_written_;
}

size_t LinuxHostTransportDriver::bytesRead() const {
  return bytes_read_;
}

} // namespace audio_studio::drivers::transport
