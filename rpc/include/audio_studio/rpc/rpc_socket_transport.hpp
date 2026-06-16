#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "audio_studio/rpc/json_rpc.hpp"
#include "socket_driver.hpp"

namespace audio_studio::rpc {

struct SocketRpcEndpoint {
  std::string host = "127.0.0.1";
  uint16_t port = 9900;
  uint32_t timeout_ms = 5000;
};

class SocketJsonRpcTransport final : public IJsonRpcTransport {
public:
  SocketJsonRpcTransport(drivers::socket::ISocketDriver& driver, SocketRpcEndpoint endpoint);
  std::string send(const std::string& request_json) override;

private:
  void connect();
  void writeAll(const uint8_t* data, size_t size);
  char readByte();

  drivers::socket::ISocketDriver& driver_;
  SocketRpcEndpoint endpoint_;
  std::unique_ptr<drivers::socket::ISocket> socket_;
};

} // namespace audio_studio::rpc
