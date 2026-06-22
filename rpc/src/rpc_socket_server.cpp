#include "rpc_server.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "rpc_framing.hpp"
#include "socket_driver.hpp"

namespace audio_studio::rpc {
namespace {

void writeAll(drivers::socket::ISocket& socket, const uint8_t* data, size_t size, uint32_t timeout_ms) {
  size_t offset = 0;
  while (offset < size) {
    size_t sent = 0;
    auto status = socket.send(data + offset, size - offset, sent, timeout_ms);
    if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
    if (sent == 0) throw JsonRpcError(JsonRpcErrorCode::kInternalError, "socket server send made no progress");
    offset += sent;
  }
}

char readByte(drivers::socket::ISocket& socket, uint32_t timeout_ms) {
  uint8_t byte = 0;
  size_t received = 0;
  auto status = socket.recv(&byte, 1, received, timeout_ms);
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
  if (received != 1) throw JsonRpcError(JsonRpcErrorCode::kInternalError, "socket server recv made no progress");
  return static_cast<char>(byte);
}

bool readPrefix(drivers::socket::ISocket& socket, uint32_t timeout_ms, std::string& prefix) {
  prefix.clear();
  prefix.reserve(4);
  for (size_t i = 0; i < 4; ++i) {
    uint8_t byte = 0;
    size_t received = 0;
    auto status = socket.recv(&byte, 1, received, timeout_ms);
    if (!status.ok()) {
      if (prefix.empty() && status.code() == framework::StatusCode::kUnavailable) return false;
      throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
    }
    if (received != 1) {
      if (prefix.empty()) return false;
      throw JsonRpcError(JsonRpcErrorCode::kInternalError, "socket server recv made no progress");
    }
    prefix.push_back(static_cast<char>(byte));
  }
  return true;
}

} // namespace

RpcSocketServer::RpcSocketServer(drivers::socket::ISocketDriver& driver, JsonRpcEndpoint& endpoint)
  : RpcSocketServer(driver, endpoint, makeDefaultStreamAck) {}

RpcSocketServer::RpcSocketServer(drivers::socket::ISocketDriver& driver,
                                 JsonRpcEndpoint& endpoint,
                                 RpcStreamHandler stream_handler)
  : driver_(driver), endpoint_(endpoint), stream_handler_(std::move(stream_handler)) {}

void RpcSocketServer::serve(const std::string& host, uint16_t port, RpcServerLimits limits) {
  auto server = driver_.createSocket(drivers::socket::SocketType::Tcp);
  if (!server) throw JsonRpcError(JsonRpcErrorCode::kInternalError, "failed to create socket RPC server");
  auto status = server->open({drivers::socket::SocketType::Tcp});
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
  status = server->bind({host, port});
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
  status = server->listen(8);
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());

  std::atomic<size_t> handled {0};
  std::mutex error_mutex;
  std::exception_ptr first_error;
  std::vector<std::thread> workers;
  const auto joinWorkers = [&workers] {
    for (auto& worker : workers) {
      if (worker.joinable()) worker.join();
    }
  };

  while (limits.max_requests == 0 || handled.load() < limits.max_requests) {
    std::unique_ptr<drivers::socket::ISocket> client;
    const uint32_t accept_timeout_ms = limits.max_requests == 0 ? std::numeric_limits<uint32_t>::max()
                                                                : std::min<uint32_t>(limits.timeout_ms, 100);
    status = server->accept(client, accept_timeout_ms);
    if (!status.ok()) {
      if (limits.max_requests != 0 && handled.load() >= limits.max_requests) break;
      if (limits.max_requests != 0 && status.code() == framework::StatusCode::kUnavailable) continue;
      server->close();
      joinWorkers();
      throw JsonRpcError(JsonRpcErrorCode::kInternalError, status.message());
    }
    if (!client) continue;

    std::thread worker([this, limits, client = std::move(client), &handled, &error_mutex, &first_error]() mutable {
      try {
        const uint32_t idle_timeout_ms = limits.max_requests == 0
                                             ? std::numeric_limits<uint32_t>::max()
                                             : limits.timeout_ms;
        while (limits.max_requests == 0 || handled.load() < limits.max_requests) {
          std::string prefix;
          if (!readPrefix(*client, idle_timeout_ms, prefix)) break;

          if (prefix == "ASRP") {
            const std::vector<uint8_t> binary_prefix(prefix.begin(), prefix.end());
            const RpcBinaryFrame request = readBinaryFrameWithPrefix(binary_prefix, [&] {
              return readByte(*client, limits.timeout_ms);
            });
            const RpcBinaryFrame response = stream_handler_ ? stream_handler_(request) : makeDefaultStreamAck(request);
            writeBinaryFrame([&](const uint8_t* data, size_t size) {
              writeAll(*client, data, size, limits.timeout_ms);
            }, response);
          } else {
            const std::string request = readContentLengthFrameWithPrefix(prefix, [&] {
              return readByte(*client, limits.timeout_ms);
            });
            const std::string response = endpoint_.handleRequest(request);
            if (!response.empty()) {
              writeContentLengthFrame([&](const uint8_t* data, size_t size) {
                writeAll(*client, data, size, limits.timeout_ms);
              }, response);
            }
          }
          if (limits.max_requests != 0) {
            ++handled;
          }
          if (limits.max_requests != 0 && handled.load() >= limits.max_requests) break;
        }
        client->shutdown();
        client->close();
      } catch (...) {
        try {
          client->shutdown();
          client->close();
        } catch (...) {
        }
        std::lock_guard<std::mutex> lock(error_mutex);
        if (!first_error) first_error = std::current_exception();
      }
    });

    if (limits.max_requests == 0) {
      worker.detach();
    } else {
      workers.emplace_back(std::move(worker));
    }
  }

  joinWorkers();
  server->close();
  if (first_error) std::rethrow_exception(first_error);
}

} // namespace audio_studio::rpc
