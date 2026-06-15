#include "audio_studio/drivers/transport/transport_driver.hpp"

#include <algorithm>
#include <utility>

namespace audio_studio::drivers::transport {

framework::Status TransportDriver::open(std::string name) {
  if (name.empty()) return framework::Status::invalidArgument("transport name is empty");
  name_ = std::move(name);
  connected_ = true;
  return framework::Status::success();
}

framework::Status TransportDriver::write(const std::vector<uint8_t>& data) {
  if (!connected_) return framework::Status::unavailable("transport is not connected");
  rx_buffer_.insert(rx_buffer_.end(), data.begin(), data.end());
  bytes_written_ += data.size();
  return framework::Status::success();
}

framework::Status TransportDriver::read(size_t capacity, std::vector<uint8_t>& out) {
  if (!connected_) return framework::Status::unavailable("transport is not connected");
  const auto count = std::min(capacity, rx_buffer_.size());
  out.assign(rx_buffer_.begin(), rx_buffer_.begin() + count);
  rx_buffer_.erase(rx_buffer_.begin(), rx_buffer_.begin() + count);
  bytes_read_ += count;
  return framework::Status::success();
}

framework::Status TransportDriver::flush() {
  if (!connected_) return framework::Status::unavailable("transport is not connected");
  rx_buffer_.clear();
  return framework::Status::success();
}

void TransportDriver::close() {
  connected_ = false;
  name_.clear();
  rx_buffer_.clear();
}

bool TransportDriver::isConnected() const {
  return connected_;
}

const std::string& TransportDriver::name() const {
  return name_;
}

TransportCaps TransportDriver::caps() const {
  return {};
}

size_t TransportDriver::bytesWritten() const {
  return bytes_written_;
}

size_t TransportDriver::bytesRead() const {
  return bytes_read_;
}

} // namespace audio_studio::drivers::transport
