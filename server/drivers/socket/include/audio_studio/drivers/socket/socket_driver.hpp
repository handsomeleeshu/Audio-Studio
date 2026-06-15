#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::drivers::socket {

enum class SocketType {
  kTcp,
  kUdp,
};

struct SocketEndpoint {
  std::string host;
  uint16_t port = 0;
};

class SocketDriver {
public:
  framework::Status open(SocketType type);
  framework::Status connect(SocketEndpoint endpoint);
  framework::Status send(const std::vector<uint8_t>& data);
  framework::Status receive(size_t capacity, std::vector<uint8_t>& out);
  void close();
  bool isOpen() const;
  bool isConnected() const;
  size_t bytesSent() const;
  size_t bytesReceived() const;

private:
  SocketType type_ = SocketType::kTcp;
  SocketEndpoint endpoint_;
  bool open_ = false;
  bool connected_ = false;
  size_t bytes_sent_ = 0;
  size_t bytes_received_ = 0;
  std::vector<uint8_t> loopback_;
};

} // namespace audio_studio::drivers::socket
