#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "json_rpc.hpp"
#include "rpc_stream_transport.hpp"
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
  ~SocketJsonRpcTransport() override;
  std::string send(const std::string& request_json) override;

private:
  void connect();
  void close();
  void writeAll(const uint8_t* data, size_t size);
  char readByte();

  drivers::socket::ISocketDriver& driver_;
  SocketRpcEndpoint endpoint_;
  std::unique_ptr<drivers::socket::ISocket> socket_;
};

class SocketRpcStreamTransport final : public IRpcStreamTransport {
public:
  SocketRpcStreamTransport(drivers::socket::ISocketDriver& driver, SocketRpcEndpoint endpoint);
  ~SocketRpcStreamTransport() override;
  void open() override;
  RpcBinaryFrame exchange(const RpcBinaryFrame& frame) override;
  void close() override;
  bool isOpen() const override;

private:
  void writeAll(const uint8_t* data, size_t size);
  char readByte();

  drivers::socket::ISocketDriver& driver_;
  SocketRpcEndpoint endpoint_;
  std::unique_ptr<drivers::socket::ISocket> socket_;
};

} // namespace audio_studio::rpc
