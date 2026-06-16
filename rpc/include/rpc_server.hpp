#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "json_rpc.hpp"
#include "rpc_framing.hpp"

namespace audio_studio::framework::audio {
class AudioService;
}

namespace audio_studio::drivers::pipe {
class IPipeDriver;
}

namespace audio_studio::drivers::socket {
class ISocketDriver;
}

namespace audio_studio::rpc {

void registerServerHealthRpcMethod(JsonRpcEndpoint& endpoint);
using RpcStreamHandler = std::function<RpcBinaryFrame(const RpcBinaryFrame& request)>;
RpcBinaryFrame makeDefaultStreamAck(const RpcBinaryFrame& request);

struct RpcServerLimits {
  size_t max_requests = 0;
  uint32_t timeout_ms = 5000;
};

class RpcSocketServer final {
public:
  RpcSocketServer(drivers::socket::ISocketDriver& driver, JsonRpcEndpoint& endpoint);
  RpcSocketServer(drivers::socket::ISocketDriver& driver, JsonRpcEndpoint& endpoint, RpcStreamHandler stream_handler);
  void serve(const std::string& host, uint16_t port, RpcServerLimits limits);

private:
  drivers::socket::ISocketDriver& driver_;
  JsonRpcEndpoint& endpoint_;
  RpcStreamHandler stream_handler_;
};

class RpcPipeServer final {
public:
  RpcPipeServer(drivers::pipe::IPipeDriver& driver, JsonRpcEndpoint& endpoint);
  RpcPipeServer(drivers::pipe::IPipeDriver& driver, JsonRpcEndpoint& endpoint, RpcStreamHandler stream_handler);
  void serve(const std::string& request_path, const std::string& response_path, RpcServerLimits limits);

private:
  drivers::pipe::IPipeDriver& driver_;
  JsonRpcEndpoint& endpoint_;
  RpcStreamHandler stream_handler_;
};

} // namespace audio_studio::rpc
