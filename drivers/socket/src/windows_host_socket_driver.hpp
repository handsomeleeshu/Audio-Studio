#pragma once

#include "socket_driver.hpp"

#include <winsock2.h>

namespace audio_studio::drivers::socket {

class WindowsHostSocket final : public ISocket {
public:
  explicit WindowsHostSocket(SocketType type);
  ~WindowsHostSocket() override;

  DriverResult open(const SocketConfig& config) override;
  DriverResult bind(const SocketEndpoint& endpoint) override;
  DriverResult listen(int backlog) override;
  DriverResult accept(std::unique_ptr<ISocket>& client, uint32_t timeout_ms) override;
  DriverResult connect(const SocketEndpoint& endpoint, uint32_t timeout_ms) override;
  DriverResult send(const uint8_t* data, size_t size, size_t& sent, uint32_t timeout_ms) override;
  DriverResult recv(uint8_t* buffer, size_t capacity, size_t& received, uint32_t timeout_ms) override;
  DriverResult shutdown() override;
  void close() override;
  bool isOpen() const override;
  bool isConnected() const override;

private:
  WindowsHostSocket(SOCKET socket, SocketType type, bool connected);

  DriverResult waitFor(long events, uint32_t timeout_ms) const;

  SocketType type_ = SocketType::Tcp;
  SOCKET socket_ = INVALID_SOCKET;
  bool connected_ = false;
};

class WindowsHostSocketDriver final : public ISocketDriver {
public:
  std::unique_ptr<ISocket> createSocket(SocketType type) override;
  DriverResult initialize() override;
  void shutdown() override;

private:
  bool initialized_ = false;
};

} // namespace audio_studio::drivers::socket
