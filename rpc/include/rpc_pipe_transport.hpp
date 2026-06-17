#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "json_rpc.hpp"
#include "rpc_stream_transport.hpp"
#include "pipe_driver.hpp"

namespace audio_studio::rpc {

struct PipeRpcEndpoint {
  std::string request_path;
  std::string response_path;
  uint32_t timeout_ms = 5000;
};

class PipeJsonRpcTransport final : public IJsonRpcTransport {
public:
  PipeJsonRpcTransport(drivers::pipe::IPipeDriver& driver, PipeRpcEndpoint endpoint);
  std::string send(const std::string& request_json) override;

private:
  void open();
  void writeAll(const uint8_t* data, size_t size);
  char readByte();

  drivers::pipe::IPipeDriver& driver_;
  PipeRpcEndpoint endpoint_;
  std::unique_ptr<drivers::pipe::IPipeStream> request_;
  std::unique_ptr<drivers::pipe::IPipeStream> response_;
};

class PipeRpcStreamTransport final : public IRpcStreamTransport {
public:
  PipeRpcStreamTransport(drivers::pipe::IPipeDriver& driver, PipeRpcEndpoint endpoint);
  ~PipeRpcStreamTransport() override;
  void open() override;
  RpcBinaryFrame exchange(const RpcBinaryFrame& frame) override;
  void close() override;
  bool isOpen() const override;

private:
  void writeAll(const uint8_t* data, size_t size);
  char readByte();

  drivers::pipe::IPipeDriver& driver_;
  PipeRpcEndpoint endpoint_;
  std::unique_ptr<drivers::pipe::IPipeStream> request_;
  std::unique_ptr<drivers::pipe::IPipeStream> response_;
};

} // namespace audio_studio::rpc
