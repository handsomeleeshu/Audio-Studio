#include "audio_studio/drivers/socket/socket_driver.hpp"

#include <algorithm>
#include <utility>

namespace audio_studio::drivers::socket {

framework::Status SocketDriver::open(SocketType type) {
  if (open_) return framework::Status::invalidArgument("socket is already open");
  type_ = type;
  open_ = true;
  return framework::Status::success();
}

framework::Status SocketDriver::connect(SocketEndpoint endpoint) {
  if (!open_) return framework::Status::unavailable("socket is not open");
  if (endpoint.host.empty()) return framework::Status::invalidArgument("socket endpoint host is empty");
  if (endpoint.port == 0) return framework::Status::invalidArgument("socket endpoint port is zero");
  endpoint_ = std::move(endpoint);
  connected_ = true;
  return framework::Status::success();
}

framework::Status SocketDriver::send(const std::vector<uint8_t>& data) {
  if (!connected_) return framework::Status::unavailable("socket is not connected");
  loopback_.insert(loopback_.end(), data.begin(), data.end());
  bytes_sent_ += data.size();
  return framework::Status::success();
}

framework::Status SocketDriver::receive(size_t capacity, std::vector<uint8_t>& out) {
  if (!connected_) return framework::Status::unavailable("socket is not connected");
  const auto count = std::min(capacity, loopback_.size());
  out.assign(loopback_.begin(), loopback_.begin() + count);
  loopback_.erase(loopback_.begin(), loopback_.begin() + count);
  bytes_received_ += count;
  return framework::Status::success();
}

void SocketDriver::close() {
  open_ = false;
  connected_ = false;
  loopback_.clear();
}

bool SocketDriver::isOpen() const {
  return open_;
}

bool SocketDriver::isConnected() const {
  return connected_;
}

size_t SocketDriver::bytesSent() const {
  return bytes_sent_;
}

size_t SocketDriver::bytesReceived() const {
  return bytes_received_;
}

} // namespace audio_studio::drivers::socket
