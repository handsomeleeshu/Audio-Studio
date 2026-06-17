#include "rpc_pipe_transport.hpp"

#include "rpc_framing.hpp"

#include <utility>

namespace audio_studio::rpc {

PipeJsonRpcTransport::PipeJsonRpcTransport(drivers::pipe::IPipeDriver& driver, PipeRpcEndpoint endpoint)
  : driver_(driver), endpoint_(std::move(endpoint)) {}

std::string PipeJsonRpcTransport::send(const std::string& request_json) {
  open();
  writeContentLengthFrame([this](const uint8_t* data, size_t size) { writeAll(data, size); }, request_json);
  const std::string response = readContentLengthFrame([this] { return readByte(); });
  request_->close();
  response_->close();
  return response;
}

void PipeJsonRpcTransport::open() {
  if (endpoint_.request_path.empty() || endpoint_.response_path.empty()) {
    throw JsonRpcError(JsonRpcErrorCode::kInvalidParams, "pipe RPC requires request and response pipe paths");
  }

  request_ = driver_.createPipeStream(drivers::pipe::PipeType::Fifo);
  response_ = driver_.createPipeStream(drivers::pipe::PipeType::Fifo);
  if (!request_ || !response_) throw JsonRpcError(JsonRpcErrorCode::kInternalError, "failed to create pipe RPC streams");

  auto status = request_->open({{endpoint_.request_path}, drivers::pipe::PipeType::Fifo});
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
  status = response_->open({{endpoint_.response_path}, drivers::pipe::PipeType::Fifo});
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
}

void PipeJsonRpcTransport::writeAll(const uint8_t* data, size_t size) {
  size_t offset = 0;
  while (offset < size) {
    size_t written = 0;
    auto status = request_->write(data + offset, size - offset, written, endpoint_.timeout_ms);
    if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
    if (written == 0) throw JsonRpcError(JsonRpcErrorCode::kInternalError, "pipe write made no progress");
    offset += written;
  }
  auto status = request_->flush();
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
}

char PipeJsonRpcTransport::readByte() {
  uint8_t byte = 0;
  size_t read_bytes = 0;
  auto status = response_->read(&byte, 1, read_bytes, endpoint_.timeout_ms);
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
  if (read_bytes != 1) throw JsonRpcError(JsonRpcErrorCode::kInternalError, "pipe read made no progress");
  return static_cast<char>(byte);
}

PipeRpcStreamTransport::PipeRpcStreamTransport(drivers::pipe::IPipeDriver& driver, PipeRpcEndpoint endpoint)
  : driver_(driver), endpoint_(std::move(endpoint)) {}

PipeRpcStreamTransport::~PipeRpcStreamTransport() {
  close();
}

RpcBinaryFrame PipeRpcStreamTransport::exchange(const RpcBinaryFrame& frame) {
  open();
  writeBinaryFrame([this](const uint8_t* data, size_t size) { writeAll(data, size); }, frame);
  return readBinaryFrame([this] { return readByte(); });
}

void PipeRpcStreamTransport::open() {
  if (request_ && response_ && request_->isOpen() && response_->isOpen()) return;
  if (endpoint_.request_path.empty() || endpoint_.response_path.empty()) {
    throw JsonRpcError(JsonRpcErrorCode::kInvalidParams, "pipe RPC stream requires request and response pipe paths");
  }

  request_ = driver_.createPipeStream(drivers::pipe::PipeType::Fifo);
  response_ = driver_.createPipeStream(drivers::pipe::PipeType::Fifo);
  if (!request_ || !response_) throw JsonRpcError(JsonRpcErrorCode::kInternalError, "failed to create pipe RPC stream");

  auto status = request_->open({{endpoint_.request_path}, drivers::pipe::PipeType::Fifo});
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
  status = response_->open({{endpoint_.response_path}, drivers::pipe::PipeType::Fifo});
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
}

void PipeRpcStreamTransport::close() {
  if (request_) request_->close();
  if (response_) response_->close();
}

bool PipeRpcStreamTransport::isOpen() const {
  return request_ && response_ && request_->isOpen() && response_->isOpen();
}

void PipeRpcStreamTransport::writeAll(const uint8_t* data, size_t size) {
  size_t offset = 0;
  while (offset < size) {
    size_t written = 0;
    auto status = request_->write(data + offset, size - offset, written, endpoint_.timeout_ms);
    if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
    if (written == 0) throw JsonRpcError(JsonRpcErrorCode::kInternalError, "pipe stream write made no progress");
    offset += written;
  }
  auto status = request_->flush();
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
}

char PipeRpcStreamTransport::readByte() {
  uint8_t byte = 0;
  size_t read_bytes = 0;
  auto status = response_->read(&byte, 1, read_bytes, endpoint_.timeout_ms);
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
  if (read_bytes != 1) throw JsonRpcError(JsonRpcErrorCode::kInternalError, "pipe stream read made no progress");
  return static_cast<char>(byte);
}

} // namespace audio_studio::rpc
