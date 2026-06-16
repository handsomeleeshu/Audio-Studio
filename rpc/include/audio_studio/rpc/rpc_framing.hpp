#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>

namespace audio_studio::rpc {

class RpcFrameError : public std::runtime_error {
public:
  explicit RpcFrameError(const std::string& message);
};

using RpcReadByte = std::function<char()>;
using RpcWriteBytes = std::function<void(const uint8_t* data, size_t size)>;

std::string encodeContentLengthFrame(const std::string& json);
std::string readContentLengthFrame(const RpcReadByte& read_byte, size_t max_payload_size = 16 * 1024 * 1024);
void writeContentLengthFrame(const RpcWriteBytes& write_bytes, const std::string& json);

} // namespace audio_studio::rpc
