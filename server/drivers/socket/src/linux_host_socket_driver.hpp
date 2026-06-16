#pragma once

#include "socket_driver.hpp"

namespace audio_studio::drivers::socket {

class LinuxHostSocket final : public ISocket {
public:
  explicit LinuxHostSocket(SocketType type);
  ~LinuxHostSocket() override;

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

  size_t bytesSent() const;
  size_t bytesReceived() const;

private:
  explicit LinuxHostSocket(int fd, SocketType type, bool connected);

  DriverResult waitFor(short events, uint32_t timeout_ms) const;

  SocketType type_ = SocketType::Tcp;
  int fd_ = -1;
  bool connected_ = false;
  size_t bytes_sent_ = 0;
  size_t bytes_received_ = 0;
};

class LinuxHostSocketDriver final : public ISocketDriver {
public:
  std::unique_ptr<ISocket> createSocket(SocketType type) override;
  DriverResult initialize() override;
  void shutdown() override;

private:
  bool initialized_ = false;
};

} // namespace audio_studio::drivers::socket
