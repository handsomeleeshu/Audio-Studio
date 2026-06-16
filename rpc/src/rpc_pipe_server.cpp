#include "rpc_server.hpp"

#include <memory>
#include <utility>
#include <vector>

#include "rpc_framing.hpp"
#include "pipe_driver.hpp"

namespace audio_studio::rpc {
namespace {

void ensurePipe(drivers::pipe::IPipeDriver& driver, const std::string& path, bool& created) {
  bool exists = false;
  auto status = driver.exists({path}, exists);
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
  if (exists) {
    created = false;
    return;
  }
  status = driver.createPipe({path}, drivers::pipe::PipeType::Fifo);
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
  created = true;
}

void writeAll(drivers::pipe::IPipeStream& pipe, const uint8_t* data, size_t size, uint32_t timeout_ms) {
  size_t offset = 0;
  while (offset < size) {
    size_t written = 0;
    auto status = pipe.write(data + offset, size - offset, written, timeout_ms);
    if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
    if (written == 0) throw JsonRpcError(JsonRpcErrorCode::kInternalError, "pipe server write made no progress");
    offset += written;
  }
  auto status = pipe.flush();
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
}

char readByte(drivers::pipe::IPipeStream& pipe, uint32_t timeout_ms) {
  uint8_t byte = 0;
  size_t read_bytes = 0;
  auto status = pipe.read(&byte, 1, read_bytes, timeout_ms);
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
  if (read_bytes != 1) throw JsonRpcError(JsonRpcErrorCode::kInternalError, "pipe server read made no progress");
  return static_cast<char>(byte);
}

} // namespace

RpcPipeServer::RpcPipeServer(drivers::pipe::IPipeDriver& driver, JsonRpcEndpoint& endpoint)
  : RpcPipeServer(driver, endpoint, makeDefaultStreamAck) {}

RpcPipeServer::RpcPipeServer(drivers::pipe::IPipeDriver& driver,
                             JsonRpcEndpoint& endpoint,
                             RpcStreamHandler stream_handler)
  : driver_(driver), endpoint_(endpoint), stream_handler_(std::move(stream_handler)) {}

void RpcPipeServer::serve(const std::string& request_path, const std::string& response_path, RpcServerLimits limits) {
  if (request_path.empty() || response_path.empty()) {
    throw JsonRpcError(JsonRpcErrorCode::kInvalidParams, "pipe RPC server requires request and response paths");
  }

  bool request_created = false;
  bool response_created = false;
  ensurePipe(driver_, request_path, request_created);
  ensurePipe(driver_, response_path, response_created);

  auto request = driver_.createPipeStream(drivers::pipe::PipeType::Fifo);
  auto response = driver_.createPipeStream(drivers::pipe::PipeType::Fifo);
  if (!request || !response) throw JsonRpcError(JsonRpcErrorCode::kInternalError, "failed to create pipe RPC server streams");

  auto status = request->open({{request_path}, drivers::pipe::PipeType::Fifo});
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
  status = response->open({{response_path}, drivers::pipe::PipeType::Fifo});
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());

  size_t handled = 0;
  while (limits.max_requests == 0 || handled < limits.max_requests) {
    std::string prefix;
    prefix.reserve(4);
    for (size_t i = 0; i < 4; ++i) prefix.push_back(readByte(*request, limits.timeout_ms));

    if (prefix == "ASRP") {
      const std::vector<uint8_t> binary_prefix(prefix.begin(), prefix.end());
      const RpcBinaryFrame request_frame = readBinaryFrameWithPrefix(binary_prefix, [&] {
        return readByte(*request, limits.timeout_ms);
      });
      const RpcBinaryFrame response_frame = stream_handler_ ? stream_handler_(request_frame) : makeDefaultStreamAck(request_frame);
      writeBinaryFrame([&](const uint8_t* data, size_t size) {
        writeAll(*response, data, size, limits.timeout_ms);
      }, response_frame);
    } else {
      const std::string request_json = readContentLengthFrameWithPrefix(prefix, [&] {
        return readByte(*request, limits.timeout_ms);
      });
      const std::string response_json = endpoint_.handleRequest(request_json);
      if (!response_json.empty()) {
        writeContentLengthFrame([&](const uint8_t* data, size_t size) {
          writeAll(*response, data, size, limits.timeout_ms);
        }, response_json);
      }
    }
    ++handled;
  }

  request->close();
  response->close();
  if (request_created) (void)driver_.removePipe({request_path}, drivers::pipe::PipeType::Fifo);
  if (response_created) (void)driver_.removePipe({response_path}, drivers::pipe::PipeType::Fifo);
}

} // namespace audio_studio::rpc
