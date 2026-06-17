#include "rpc_socket_transport.hpp"

#include "rpc_framing.hpp"

#include <utility>

namespace audio_studio::rpc {

SocketJsonRpcTransport::SocketJsonRpcTransport(drivers::socket::ISocketDriver& driver, SocketRpcEndpoint endpoint)
  : driver_(driver), endpoint_(std::move(endpoint)) {}

SocketJsonRpcTransport::~SocketJsonRpcTransport() {
  close();
}

std::string SocketJsonRpcTransport::send(const std::string& request_json) {
  try {
    connect();
    writeContentLengthFrame([this](const uint8_t* data, size_t size) { writeAll(data, size); }, request_json);
    return readContentLengthFrame([this] { return readByte(); });
  } catch (...) {
    close();
    throw;
  }
}

void SocketJsonRpcTransport::connect() {
  if (socket_ && socket_->isConnected()) return;
  socket_ = driver_.createSocket(drivers::socket::SocketType::Tcp);
  if (!socket_) throw JsonRpcError(JsonRpcErrorCode::kInternalError, "failed to create socket RPC client");
  auto status = socket_->open({drivers::socket::SocketType::Tcp});
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
  status = socket_->connect({endpoint_.host, endpoint_.port}, endpoint_.timeout_ms);
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
}

void SocketJsonRpcTransport::close() {
  if (!socket_) return;
  (void)socket_->shutdown();
  socket_->close();
  socket_.reset();
}

void SocketJsonRpcTransport::writeAll(const uint8_t* data, size_t size) {
  size_t offset = 0;
  while (offset < size) {
    size_t sent = 0;
    auto status = socket_->send(data + offset, size - offset, sent, endpoint_.timeout_ms);
    if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
    if (sent == 0) throw JsonRpcError(JsonRpcErrorCode::kInternalError, "socket send made no progress");
    offset += sent;
  }
}

char SocketJsonRpcTransport::readByte() {
  uint8_t byte = 0;
  size_t received = 0;
  auto status = socket_->recv(&byte, 1, received, endpoint_.timeout_ms);
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
  if (received != 1) throw JsonRpcError(JsonRpcErrorCode::kInternalError, "socket recv made no progress");
  return static_cast<char>(byte);
}

SocketRpcStreamTransport::SocketRpcStreamTransport(drivers::socket::ISocketDriver& driver, SocketRpcEndpoint endpoint)
  : driver_(driver), endpoint_(std::move(endpoint)) {}

SocketRpcStreamTransport::~SocketRpcStreamTransport() {
  close();
}

void SocketRpcStreamTransport::open() {
  if (socket_ && socket_->isConnected()) return;
  socket_ = driver_.createSocket(drivers::socket::SocketType::Tcp);
  if (!socket_) throw JsonRpcError(JsonRpcErrorCode::kInternalError, "failed to create socket RPC stream client");
  auto status = socket_->open({drivers::socket::SocketType::Tcp});
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
  status = socket_->connect({endpoint_.host, endpoint_.port}, endpoint_.timeout_ms);
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
}

RpcBinaryFrame SocketRpcStreamTransport::exchange(const RpcBinaryFrame& frame) {
  open();
  writeBinaryFrame([this](const uint8_t* data, size_t size) { writeAll(data, size); }, frame);
  return readBinaryFrame([this] { return readByte(); });
}

void SocketRpcStreamTransport::close() {
  if (!socket_) return;
  (void)socket_->shutdown();
  socket_->close();
  socket_.reset();
}

bool SocketRpcStreamTransport::isOpen() const {
  return socket_ && socket_->isConnected();
}

void SocketRpcStreamTransport::writeAll(const uint8_t* data, size_t size) {
  size_t offset = 0;
  while (offset < size) {
    size_t sent = 0;
    auto status = socket_->send(data + offset, size - offset, sent, endpoint_.timeout_ms);
    if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
    if (sent == 0) throw JsonRpcError(JsonRpcErrorCode::kInternalError, "socket stream send made no progress");
    offset += sent;
  }
}

char SocketRpcStreamTransport::readByte() {
  uint8_t byte = 0;
  size_t received = 0;
  auto status = socket_->recv(&byte, 1, received, endpoint_.timeout_ms);
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
  if (received != 1) throw JsonRpcError(JsonRpcErrorCode::kInternalError, "socket stream recv made no progress");
  return static_cast<char>(byte);
}

} // namespace audio_studio::rpc
