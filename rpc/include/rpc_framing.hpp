#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace audio_studio::rpc {

class RpcFrameError : public std::runtime_error {
public:
  explicit RpcFrameError(const std::string& message);
};

using RpcReadByte = std::function<char()>;
using RpcWriteBytes = std::function<void(const uint8_t* data, size_t size)>;

enum class RpcMessageType : uint16_t {
  kRequest = 1,
  kResponse = 2,
  kEvent = 3,
  kStreamData = 4,
  kStreamAck = 5,
  kStreamEnd = 6,
  kError = 7,
};

enum class RpcPayloadType : uint16_t {
  kJson = 1,
  kBinary = 2,
  kJsonAndBinary = 3,
};

enum class RpcServiceId : uint16_t {
  kSystem = 1,
  kSession = 2,
  kLog = 3,
  kDump = 4,
  kAudio = 5,
  kConfig = 6,
  kControl = 7,
};

constexpr uint16_t kRpcAudioMethodWriteFrames = 1;
constexpr uint16_t kRpcAudioMethodReadFrames = 2;

struct RpcBinaryFrameHeader {
  uint32_t magic = 0x50525341;
  uint16_t version = 1;
  uint16_t header_size = 44;
  RpcMessageType message_type = RpcMessageType::kStreamData;
  uint16_t service_id = static_cast<uint16_t>(RpcServiceId::kAudio);
  uint16_t method_id = 0;
  RpcPayloadType payload_type = RpcPayloadType::kBinary;
  uint32_t request_id = 0;
  uint32_t session_id = 0;
  uint32_t stream_id = 0;
  uint32_t flags = 0;
  uint32_t payload_len = 0;
  uint32_t payload_crc32 = 0;
  uint32_t header_crc32 = 0;
};

struct RpcBinaryFrame {
  RpcBinaryFrameHeader header;
  std::vector<uint8_t> payload;
};

std::string encodeContentLengthFrame(const std::string& json);
std::string readContentLengthFrame(const RpcReadByte& read_byte, size_t max_payload_size = 16 * 1024 * 1024);
std::string readContentLengthFrameWithPrefix(const std::string& prefix, const RpcReadByte& read_byte,
                                             size_t max_payload_size = 16 * 1024 * 1024);
void writeContentLengthFrame(const RpcWriteBytes& write_bytes, const std::string& json);

std::vector<uint8_t> encodeBinaryFrame(const RpcBinaryFrame& frame);
RpcBinaryFrame decodeBinaryFrame(const uint8_t* data, size_t size);
RpcBinaryFrame readBinaryFrame(const RpcReadByte& read_byte, size_t max_payload_size = 64 * 1024 * 1024);
RpcBinaryFrame readBinaryFrameWithPrefix(const std::vector<uint8_t>& prefix, const RpcReadByte& read_byte,
                                         size_t max_payload_size = 64 * 1024 * 1024);
void writeBinaryFrame(const RpcWriteBytes& write_bytes, const RpcBinaryFrame& frame);
uint32_t rpcCrc32(const uint8_t* data, size_t size);

} // namespace audio_studio::rpc
