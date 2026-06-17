#include "windows_host_socket_driver.hpp"

#include <ws2tcpip.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

namespace audio_studio::drivers::socket {
namespace {

class WindowsHostSocketDriverFactory final : public ISocketDriverFactory {
public:
  std::string name() const override { return "windows-host"; }
  std::unique_ptr<ISocketDriver> create() const override { return std::make_unique<WindowsHostSocketDriver>(); }
};

const bool kWindowsHostSocketDriverRegistered = [] {
  auto status = SocketDriverRegistry::instance().registerFactory(std::make_unique<WindowsHostSocketDriverFactory>());
  (void)status;
  return true;
}();

DriverResult socketError(const std::string& operation, int error = WSAGetLastError()) {
  return DriverResult::internal(operation + " failed: WSA error " + std::to_string(error));
}

DriverResult fillAddress(const SocketEndpoint& endpoint, sockaddr_storage& storage, int& length) {
  if (endpoint.port == 0) return DriverResult::invalidArgument("socket endpoint port is zero");
  std::memset(&storage, 0, sizeof(storage));
  const std::string host = endpoint.host.empty() ? "0.0.0.0" : endpoint.host;

  auto* address = reinterpret_cast<sockaddr_in*>(&storage);
  address->sin_family = AF_INET;
  address->sin_port = htons(endpoint.port);
  address->sin_addr.s_addr = inet_addr(host.c_str());
  if (address->sin_addr.s_addr == INADDR_NONE && host != "255.255.255.255") {
    return DriverResult::invalidArgument("invalid IPv4 endpoint host: " + host);
  }
  length = sizeof(sockaddr_in);
  return DriverResult::success();
}

timeval timeoutValue(uint32_t timeout_ms) {
  timeval timeout {};
  timeout.tv_sec = static_cast<long>(timeout_ms / 1000);
  timeout.tv_usec = static_cast<long>((timeout_ms % 1000) * 1000);
  return timeout;
}

} // namespace

WindowsHostSocket::WindowsHostSocket(SocketType type) : type_(type) {}

WindowsHostSocket::WindowsHostSocket(SOCKET socket, SocketType type, bool connected)
  : type_(type), socket_(socket), connected_(connected) {}

WindowsHostSocket::~WindowsHostSocket() {
  close();
}

DriverResult WindowsHostSocket::open(const SocketConfig& config) {
  if (socket_ != INVALID_SOCKET) return DriverResult::invalidArgument("socket is already open");
  type_ = config.type;
  const int socket_type = type_ == SocketType::Tcp ? SOCK_STREAM : SOCK_DGRAM;
  const int protocol = type_ == SocketType::Tcp ? IPPROTO_TCP : IPPROTO_UDP;
  socket_ = ::socket(AF_INET, socket_type, protocol);
  if (socket_ == INVALID_SOCKET) return socketError("socket");
  return DriverResult::success();
}

DriverResult WindowsHostSocket::bind(const SocketEndpoint& endpoint) {
  if (socket_ == INVALID_SOCKET) return DriverResult::unavailable("socket is not open");

  BOOL reuse = TRUE;
  (void)::setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

  sockaddr_storage address {};
  int length = 0;
  auto status = fillAddress(endpoint, address, length);
  if (!status.ok()) return status;
  if (::bind(socket_, reinterpret_cast<const sockaddr*>(&address), length) == SOCKET_ERROR) return socketError("bind");
  return DriverResult::success();
}

DriverResult WindowsHostSocket::listen(int backlog) {
  if (socket_ == INVALID_SOCKET) return DriverResult::unavailable("socket is not open");
  if (type_ != SocketType::Tcp) return DriverResult::invalidArgument("listen requires TCP socket");
  if (backlog <= 0) return DriverResult::invalidArgument("socket backlog must be positive");
  if (::listen(socket_, backlog) == SOCKET_ERROR) return socketError("listen");
  return DriverResult::success();
}

DriverResult WindowsHostSocket::accept(std::unique_ptr<ISocket>& client, uint32_t timeout_ms) {
  client.reset();
  if (socket_ == INVALID_SOCKET) return DriverResult::unavailable("socket is not open");
  if (type_ != SocketType::Tcp) return DriverResult::invalidArgument("accept requires TCP socket");

  auto status = waitFor(FD_READ, timeout_ms);
  if (!status.ok()) return status;

  SOCKET accepted = ::accept(socket_, nullptr, nullptr);
  if (accepted == INVALID_SOCKET) return socketError("accept");
  client = std::unique_ptr<ISocket>(new WindowsHostSocket(accepted, type_, true));
  return DriverResult::success();
}

DriverResult WindowsHostSocket::connect(const SocketEndpoint& endpoint, uint32_t timeout_ms) {
  if (socket_ == INVALID_SOCKET) return DriverResult::unavailable("socket is not open");

  sockaddr_storage address {};
  int length = 0;
  auto status = fillAddress(endpoint, address, length);
  if (!status.ok()) return status;

  u_long non_blocking = 1;
  if (::ioctlsocket(socket_, FIONBIO, &non_blocking) == SOCKET_ERROR) return socketError("ioctlsocket(FIONBIO)");

  int rc = ::connect(socket_, reinterpret_cast<const sockaddr*>(&address), length);
  if (rc == SOCKET_ERROR) {
    const int error = WSAGetLastError();
    if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS && error != WSAEINVAL) {
      non_blocking = 0;
      (void)::ioctlsocket(socket_, FIONBIO, &non_blocking);
      return socketError("connect", error);
    }
    status = waitFor(FD_WRITE, timeout_ms);
    if (!status.ok()) {
      non_blocking = 0;
      (void)::ioctlsocket(socket_, FIONBIO, &non_blocking);
      return status;
    }

    int socket_error = 0;
    int socket_error_len = sizeof(socket_error);
    if (::getsockopt(socket_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socket_error), &socket_error_len) == SOCKET_ERROR) {
      non_blocking = 0;
      (void)::ioctlsocket(socket_, FIONBIO, &non_blocking);
      return socketError("getsockopt(SO_ERROR)");
    }
    if (socket_error != 0) {
      non_blocking = 0;
      (void)::ioctlsocket(socket_, FIONBIO, &non_blocking);
      return DriverResult::unavailable("connect failed: WSA error " + std::to_string(socket_error));
    }
  }

  non_blocking = 0;
  if (::ioctlsocket(socket_, FIONBIO, &non_blocking) == SOCKET_ERROR) return socketError("ioctlsocket(blocking)");
  connected_ = true;
  return DriverResult::success();
}

DriverResult WindowsHostSocket::send(const uint8_t* data, size_t size, size_t& sent, uint32_t timeout_ms) {
  sent = 0;
  if (!connected_) return DriverResult::unavailable("socket is not connected");
  if (data == nullptr && size > 0) return DriverResult::invalidArgument("socket send buffer is null");
  if (size == 0) return DriverResult::success();

  auto status = waitFor(FD_WRITE, timeout_ms);
  if (!status.ok()) return status;

  const int chunk = static_cast<int>(std::min<size_t>(size, static_cast<size_t>(std::numeric_limits<int>::max())));
  const int rc = ::send(socket_, reinterpret_cast<const char*>(data), chunk, 0);
  if (rc == SOCKET_ERROR) return socketError("send");
  sent = static_cast<size_t>(rc);
  return DriverResult::success();
}

DriverResult WindowsHostSocket::recv(uint8_t* buffer, size_t capacity, size_t& received, uint32_t timeout_ms) {
  received = 0;
  if (!connected_) return DriverResult::unavailable("socket is not connected");
  if (buffer == nullptr && capacity > 0) return DriverResult::invalidArgument("socket receive buffer is null");
  if (capacity == 0) return DriverResult::success();

  auto status = waitFor(FD_READ, timeout_ms);
  if (!status.ok()) return status;

  const int chunk = static_cast<int>(std::min<size_t>(capacity, static_cast<size_t>(std::numeric_limits<int>::max())));
  const int rc = ::recv(socket_, reinterpret_cast<char*>(buffer), chunk, 0);
  if (rc == SOCKET_ERROR) return socketError("recv");
  if (rc == 0) return DriverResult::unavailable("socket peer closed");
  received = static_cast<size_t>(rc);
  return DriverResult::success();
}

DriverResult WindowsHostSocket::shutdown() {
  if (socket_ != INVALID_SOCKET) (void)::shutdown(socket_, SD_BOTH);
  connected_ = false;
  return DriverResult::success();
}

void WindowsHostSocket::close() {
  if (socket_ != INVALID_SOCKET) {
    ::closesocket(socket_);
    socket_ = INVALID_SOCKET;
  }
  connected_ = false;
}

bool WindowsHostSocket::isOpen() const {
  return socket_ != INVALID_SOCKET;
}

bool WindowsHostSocket::isConnected() const {
  return connected_;
}

DriverResult WindowsHostSocket::waitFor(long events, uint32_t timeout_ms) const {
  if (socket_ == INVALID_SOCKET) return DriverResult::unavailable("socket is not open");
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(socket_, &fds);
  timeval timeout = timeoutValue(timeout_ms);
  timeval* timeout_ptr = timeout_ms == std::numeric_limits<uint32_t>::max() ? nullptr : &timeout;
  const int rc = ::select(0,
                          (events & FD_READ) != 0 ? &fds : nullptr,
                          (events & FD_WRITE) != 0 ? &fds : nullptr,
                          (events & FD_CLOSE) != 0 ? &fds : nullptr,
                          timeout_ptr);
  if (rc == SOCKET_ERROR) return socketError("select");
  if (rc == 0) return DriverResult::unavailable("socket operation timed out");
  return DriverResult::success();
}

std::unique_ptr<ISocket> WindowsHostSocketDriver::createSocket(SocketType type) {
  if (!initialized_) return nullptr;
  return std::make_unique<WindowsHostSocket>(type);
}

DriverResult WindowsHostSocketDriver::initialize() {
  if (initialized_) return DriverResult::success();
  WSADATA data {};
  const int rc = WSAStartup(MAKEWORD(2, 2), &data);
  if (rc != 0) return socketError("WSAStartup", rc);
  initialized_ = true;
  return DriverResult::success();
}

void WindowsHostSocketDriver::shutdown() {
  if (initialized_) {
    WSACleanup();
    initialized_ = false;
  }
}

} // namespace audio_studio::drivers::socket
