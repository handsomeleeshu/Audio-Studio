#include "linux_host_socket_driver.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace audio_studio::drivers::socket {

namespace {

class LinuxHostSocketDriverFactory final : public ISocketDriverFactory {
public:
  std::string name() const override { return "linux-host"; }
  std::unique_ptr<ISocketDriver> create() const override { return std::make_unique<LinuxHostSocketDriver>(); }
};

const bool kLinuxHostSocketDriverRegistered = [] {
  auto status = SocketDriverRegistry::instance().registerFactory(std::make_unique<LinuxHostSocketDriverFactory>());
  (void)status;
  return true;
}();

} // namespace

LinuxHostSocket::LinuxHostSocket(SocketType type) : type_(type) {}

DriverResult LinuxHostSocket::open(const SocketConfig& config) {
  if (open_) return DriverResult::invalidArgument("socket is already open");
  type_ = config.type;
  open_ = true;
  return DriverResult::success();
}

DriverResult LinuxHostSocket::bind(const SocketEndpoint& endpoint) {
  if (!open_) return DriverResult::unavailable("socket is not open");
  if (endpoint.host.empty()) return DriverResult::invalidArgument("socket endpoint host is empty");
  if (endpoint.port == 0) return DriverResult::invalidArgument("socket endpoint port is zero");
  endpoint_ = endpoint;
  return DriverResult::success();
}

DriverResult LinuxHostSocket::listen(int backlog) {
  if (!open_) return DriverResult::unavailable("socket is not open");
  if (backlog <= 0) return DriverResult::invalidArgument("socket backlog must be positive");
  return DriverResult::success();
}

DriverResult LinuxHostSocket::accept(std::unique_ptr<ISocket>& client, uint32_t /*timeout_ms*/) {
  if (!open_) return DriverResult::unavailable("socket is not open");
  auto accepted = std::make_unique<LinuxHostSocket>(type_);
  accepted->open(SocketConfig{type_});
  accepted->connected_ = true;
  client = std::move(accepted);
  return DriverResult::success();
}

DriverResult LinuxHostSocket::connect(const SocketEndpoint& endpoint, uint32_t /*timeout_ms*/) {
  if (!open_) return DriverResult::unavailable("socket is not open");
  if (endpoint.host.empty()) return DriverResult::invalidArgument("socket endpoint host is empty");
  if (endpoint.port == 0) return DriverResult::invalidArgument("socket endpoint port is zero");
  endpoint_ = endpoint;
  connected_ = true;
  return DriverResult::success();
}

DriverResult LinuxHostSocket::send(const uint8_t* data, size_t size, size_t& sent, uint32_t /*timeout_ms*/) {
  if (!connected_) return DriverResult::unavailable("socket is not connected");
  if (data == nullptr && size > 0) return DriverResult::invalidArgument("socket send buffer is null");
  loopback_.insert(loopback_.end(), data, data + size);
  bytes_sent_ += size;
  sent = size;
  return DriverResult::success();
}

DriverResult LinuxHostSocket::recv(uint8_t* buffer, size_t capacity, size_t& received, uint32_t /*timeout_ms*/) {
  if (!connected_) return DriverResult::unavailable("socket is not connected");
  if (buffer == nullptr && capacity > 0) return DriverResult::invalidArgument("socket receive buffer is null");
  const auto count = std::min(capacity, loopback_.size());
  if (count > 0) std::memcpy(buffer, loopback_.data(), count);
  loopback_.erase(loopback_.begin(), loopback_.begin() + count);
  bytes_received_ += count;
  received = count;
  return DriverResult::success();
}

DriverResult LinuxHostSocket::shutdown() {
  connected_ = false;
  return DriverResult::success();
}

void LinuxHostSocket::close() {
  open_ = false;
  connected_ = false;
  loopback_.clear();
}

bool LinuxHostSocket::isOpen() const {
  return open_;
}

bool LinuxHostSocket::isConnected() const {
  return connected_;
}

size_t LinuxHostSocket::bytesSent() const {
  return bytes_sent_;
}

size_t LinuxHostSocket::bytesReceived() const {
  return bytes_received_;
}

std::unique_ptr<ISocket> LinuxHostSocketDriver::createSocket(SocketType type) {
  if (!initialized_) return nullptr;
  return std::make_unique<LinuxHostSocket>(type);
}

DriverResult LinuxHostSocketDriver::initialize() {
  initialized_ = true;
  return DriverResult::success();
}

void LinuxHostSocketDriver::shutdown() {
  initialized_ = false;
}

} // namespace audio_studio::drivers::socket
