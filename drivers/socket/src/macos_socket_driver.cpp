#include "macos_socket_driver.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <limits>
#include <utility>

namespace audio_studio::drivers::socket {

namespace {

class MacOsSocketDriverFactory final : public ISocketDriverFactory {
public:
  std::string name() const override { return "macos"; }
  std::unique_ptr<ISocketDriver> create() const override { return std::make_unique<MacOsSocketDriver>(); }
};

const bool kMacOsSocketDriverRegistered = [] {
  auto status = SocketDriverRegistry::instance().registerFactory(std::make_unique<MacOsSocketDriverFactory>());
  (void)status;
  return true;
}();

DriverResult systemError(const std::string& operation) {
  return DriverResult::internal(operation + " failed: " + std::strerror(errno));
}

int socketDomain(const SocketEndpoint& endpoint) {
  return endpoint.host.find(':') == std::string::npos ? AF_INET : AF_INET6;
}

DriverResult fillAddress(const SocketEndpoint& endpoint, sockaddr_storage& storage, socklen_t& length) {
  if (endpoint.port == 0) return DriverResult::invalidArgument("socket endpoint port is zero");

  std::memset(&storage, 0, sizeof(storage));
  const std::string host = endpoint.host.empty() ? "0.0.0.0" : endpoint.host;
  const int domain = socketDomain({host, endpoint.port});

  if (domain == AF_INET) {
    auto* address = reinterpret_cast<sockaddr_in*>(&storage);
    address->sin_family = AF_INET;
    address->sin_port = htons(endpoint.port);
    if (::inet_pton(AF_INET, host.c_str(), &address->sin_addr) != 1) {
      return DriverResult::invalidArgument("invalid IPv4 endpoint host: " + host);
    }
    length = sizeof(sockaddr_in);
    return DriverResult::success();
  }

  auto* address = reinterpret_cast<sockaddr_in6*>(&storage);
  address->sin6_family = AF_INET6;
  address->sin6_port = htons(endpoint.port);
  if (::inet_pton(AF_INET6, host.c_str(), &address->sin6_addr) != 1) {
    return DriverResult::invalidArgument("invalid IPv6 endpoint host: " + host);
  }
  length = sizeof(sockaddr_in6);
  return DriverResult::success();
}

} // namespace

MacOsSocket::MacOsSocket(SocketType type) : type_(type) {}

MacOsSocket::MacOsSocket(int fd, SocketType type, bool connected) : type_(type), fd_(fd), connected_(connected) {}

MacOsSocket::~MacOsSocket() {
  close();
}

DriverResult MacOsSocket::open(const SocketConfig& config) {
  if (fd_ >= 0) return DriverResult::invalidArgument("socket is already open");
  type_ = config.type;
  const int socket_type = type_ == SocketType::Tcp ? SOCK_STREAM : SOCK_DGRAM;
  fd_ = ::socket(AF_INET, socket_type, 0);
  if (fd_ < 0) return systemError("socket");
  (void)::fcntl(fd_, F_SETFD, FD_CLOEXEC);
  return DriverResult::success();
}

DriverResult MacOsSocket::bind(const SocketEndpoint& endpoint) {
  if (fd_ < 0) return DriverResult::unavailable("socket is not open");

  int reuse = 1;
  (void)::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_storage address {};
  socklen_t length = 0;
  auto status = fillAddress(endpoint, address, length);
  if (!status.ok()) return status;
  if (::bind(fd_, reinterpret_cast<const sockaddr*>(&address), length) != 0) return systemError("bind");
  return DriverResult::success();
}

DriverResult MacOsSocket::listen(int backlog) {
  if (fd_ < 0) return DriverResult::unavailable("socket is not open");
  if (type_ != SocketType::Tcp) return DriverResult::invalidArgument("listen requires TCP socket");
  if (backlog <= 0) return DriverResult::invalidArgument("socket backlog must be positive");
  if (::listen(fd_, backlog) != 0) return systemError("listen");
  return DriverResult::success();
}

DriverResult MacOsSocket::accept(std::unique_ptr<ISocket>& client, uint32_t timeout_ms) {
  client.reset();
  if (fd_ < 0) return DriverResult::unavailable("socket is not open");
  if (type_ != SocketType::Tcp) return DriverResult::invalidArgument("accept requires TCP socket");

  auto status = waitFor(POLLIN, timeout_ms);
  if (!status.ok()) return status;

  sockaddr_storage address {};
  socklen_t length = sizeof(address);
  int accepted_fd = ::accept(fd_, reinterpret_cast<sockaddr*>(&address), &length);
  if (accepted_fd < 0) return systemError("accept");
  (void)::fcntl(accepted_fd, F_SETFD, FD_CLOEXEC);
  client = std::unique_ptr<ISocket>(new MacOsSocket(accepted_fd, type_, true));
  return DriverResult::success();
}

DriverResult MacOsSocket::connect(const SocketEndpoint& endpoint, uint32_t timeout_ms) {
  if (fd_ < 0) return DriverResult::unavailable("socket is not open");

  sockaddr_storage address {};
  socklen_t length = 0;
  auto status = fillAddress(endpoint, address, length);
  if (!status.ok()) return status;

  if (type_ == SocketType::Udp) {
    if (::connect(fd_, reinterpret_cast<const sockaddr*>(&address), length) != 0) return systemError("connect");
    connected_ = true;
    return DriverResult::success();
  }

  const int flags = ::fcntl(fd_, F_GETFL, 0);
  if (flags < 0) return systemError("fcntl(F_GETFL)");
  if (::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) != 0) return systemError("fcntl(F_SETFL)");

  int rc = ::connect(fd_, reinterpret_cast<const sockaddr*>(&address), length);
  if (rc != 0 && errno != EINPROGRESS) {
    const auto error = systemError("connect");
    (void)::fcntl(fd_, F_SETFL, flags);
    return error;
  }

  if (rc != 0) {
    status = waitFor(POLLOUT, timeout_ms);
    if (!status.ok()) {
      (void)::fcntl(fd_, F_SETFL, flags);
      return status;
    }

    int socket_error = 0;
    socklen_t socket_error_len = sizeof(socket_error);
    if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_len) != 0) {
      const auto error = systemError("getsockopt(SO_ERROR)");
      (void)::fcntl(fd_, F_SETFL, flags);
      return error;
    }
    if (socket_error != 0) {
      (void)::fcntl(fd_, F_SETFL, flags);
      return DriverResult::unavailable("connect failed: " + std::string(std::strerror(socket_error)));
    }
  }

  if (::fcntl(fd_, F_SETFL, flags) != 0) return systemError("fcntl(restore)");
  connected_ = true;
  return DriverResult::success();
}

DriverResult MacOsSocket::send(const uint8_t* data, size_t size, size_t& sent, uint32_t timeout_ms) {
  sent = 0;
  if (!connected_) return DriverResult::unavailable("socket is not connected");
  if (data == nullptr && size > 0) return DriverResult::invalidArgument("socket send buffer is null");
  if (size == 0) return DriverResult::success();

  auto status = waitFor(POLLOUT, timeout_ms);
  if (!status.ok()) return status;

  const auto rc = ::send(fd_, data, size, 0);
  if (rc < 0) return systemError("send");
  sent = static_cast<size_t>(rc);
  bytes_sent_ += sent;
  return DriverResult::success();
}

DriverResult MacOsSocket::recv(uint8_t* buffer, size_t capacity, size_t& received, uint32_t timeout_ms) {
  received = 0;
  if (!connected_) return DriverResult::unavailable("socket is not connected");
  if (buffer == nullptr && capacity > 0) return DriverResult::invalidArgument("socket receive buffer is null");
  if (capacity == 0) return DriverResult::success();

  auto status = waitFor(POLLIN, timeout_ms);
  if (!status.ok()) return status;

  const auto rc = ::recv(fd_, buffer, capacity, 0);
  if (rc < 0) return systemError("recv");
  if (rc == 0) return DriverResult::unavailable("socket peer closed");
  received = static_cast<size_t>(rc);
  bytes_received_ += received;
  return DriverResult::success();
}

DriverResult MacOsSocket::shutdown() {
  if (fd_ >= 0) (void)::shutdown(fd_, SHUT_RDWR);
  connected_ = false;
  return DriverResult::success();
}

void MacOsSocket::close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  connected_ = false;
}

bool MacOsSocket::isOpen() const {
  return fd_ >= 0;
}

bool MacOsSocket::isConnected() const {
  return connected_;
}

size_t MacOsSocket::bytesSent() const {
  return bytes_sent_;
}

size_t MacOsSocket::bytesReceived() const {
  return bytes_received_;
}

DriverResult MacOsSocket::waitFor(short events, uint32_t timeout_ms) const {
  if (fd_ < 0) return DriverResult::unavailable("socket is not open");
  pollfd descriptor {};
  descriptor.fd = fd_;
  descriptor.events = events;
  const int timeout = timeout_ms == std::numeric_limits<uint32_t>::max() ? -1 : static_cast<int>(timeout_ms);
  const int rc = ::poll(&descriptor, 1, timeout);
  if (rc < 0) return systemError("poll");
  if (rc == 0) return DriverResult::unavailable("socket operation timed out");
  // POLLERR and POLLNVAL are actual errors, but POLLHUP may still have data to read
  if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0) {
    return DriverResult::unavailable("socket error event");
  }
  if ((descriptor.revents & events) == 0) return DriverResult::unavailable("socket event not ready");
  return DriverResult::success();
}

std::unique_ptr<ISocket> MacOsSocketDriver::createSocket(SocketType type) {
  if (!initialized_) return nullptr;
  return std::make_unique<MacOsSocket>(type);
}

DriverResult MacOsSocketDriver::initialize() {
  initialized_ = true;
  return DriverResult::success();
}

void MacOsSocketDriver::shutdown() {
  initialized_ = false;
}

} // namespace audio_studio::drivers::socket