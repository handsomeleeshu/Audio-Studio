#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "audio_studio/rpc/json_rpc.hpp"
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

} // namespace audio_studio::rpc
