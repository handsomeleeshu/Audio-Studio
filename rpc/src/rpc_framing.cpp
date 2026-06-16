#include "rpc_framing.hpp"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <vector>

namespace audio_studio::rpc {
namespace {

constexpr const char* kHeaderEnd = "\r\n\r\n";
constexpr uint32_t kBinaryMagic = 0x50525341;
constexpr size_t kBinaryHeaderSize = 44;

size_t parseContentLength(const std::string& header) {
  const std::string key = "Content-Length:";
  const auto pos = header.find(key);
  if (pos == std::string::npos) throw RpcFrameError("missing Content-Length header");
  size_t begin = pos + key.size();
  while (begin < header.size() && std::isspace(static_cast<unsigned char>(header[begin]))) ++begin;
  size_t end = begin;
  while (end < header.size() && std::isdigit(static_cast<unsigned char>(header[end]))) ++end;
  if (begin == end) throw RpcFrameError("invalid Content-Length header");
  return static_cast<size_t>(std::stoull(header.substr(begin, end - begin)));
}

void appendU16(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xff));
  out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

void appendU32(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xff));
  out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
  out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
  out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
}

uint16_t readU16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t readU32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

std::vector<uint8_t> encodeHeader(RpcBinaryFrameHeader header, uint32_t header_crc32) {
  std::vector<uint8_t> out;
  out.reserve(kBinaryHeaderSize);
  appendU32(out, header.magic);
  appendU16(out, header.version);
  appendU16(out, header.header_size);
  appendU16(out, static_cast<uint16_t>(header.message_type));
  appendU16(out, header.service_id);
  appendU16(out, header.method_id);
  appendU16(out, static_cast<uint16_t>(header.payload_type));
  appendU32(out, header.request_id);
  appendU32(out, header.session_id);
  appendU32(out, header.stream_id);
  appendU32(out, header.flags);
  appendU32(out, header.payload_len);
  appendU32(out, header.payload_crc32);
  appendU32(out, header_crc32);
  return out;
}

RpcBinaryFrameHeader decodeHeader(const uint8_t* data) {
  RpcBinaryFrameHeader header;
  header.magic = readU32(data);
  header.version = readU16(data + 4);
  header.header_size = readU16(data + 6);
  header.message_type = static_cast<RpcMessageType>(readU16(data + 8));
  header.service_id = readU16(data + 10);
  header.method_id = readU16(data + 12);
  header.payload_type = static_cast<RpcPayloadType>(readU16(data + 14));
  header.request_id = readU32(data + 16);
  header.session_id = readU32(data + 20);
  header.stream_id = readU32(data + 24);
  header.flags = readU32(data + 28);
  header.payload_len = readU32(data + 32);
  header.payload_crc32 = readU32(data + 36);
  header.header_crc32 = readU32(data + 40);
  return header;
}

} // namespace

RpcFrameError::RpcFrameError(const std::string& message) : std::runtime_error(message) {}

std::string encodeContentLengthFrame(const std::string& json) {
  std::ostringstream out;
  out << "Content-Length: " << json.size() << "\r\n\r\n" << json;
  return out.str();
}

std::string readContentLengthFrame(const RpcReadByte& read_byte, size_t max_payload_size) {
  return readContentLengthFrameWithPrefix("", read_byte, max_payload_size);
}

std::string readContentLengthFrameWithPrefix(const std::string& prefix, const RpcReadByte& read_byte, size_t max_payload_size) {
  std::string header = prefix;
  header.reserve(64);
  while (header.find(kHeaderEnd) == std::string::npos) {
    header.push_back(read_byte());
    if (header.size() > 4096) throw RpcFrameError("RPC frame header is too large");
  }

  const auto header_end = header.find(kHeaderEnd);
  const auto payload_prefix = header.substr(header_end + 4);
  header.resize(header_end + 4);

  const size_t content_length = parseContentLength(header);
  if (content_length > max_payload_size) throw RpcFrameError("RPC frame payload is too large");

  std::string payload = payload_prefix;
  payload.reserve(content_length);
  while (payload.size() < content_length) payload.push_back(read_byte());
  if (payload.size() > content_length) payload.resize(content_length);
  return payload;
}

void writeContentLengthFrame(const RpcWriteBytes& write_bytes, const std::string& json) {
  const std::string frame = encodeContentLengthFrame(json);
  write_bytes(reinterpret_cast<const uint8_t*>(frame.data()), frame.size());
}

uint32_t rpcCrc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0u - (crc & 1u);
      crc = (crc >> 1) ^ (0xedb88320u & mask);
    }
  }
  return ~crc;
}

std::vector<uint8_t> encodeBinaryFrame(const RpcBinaryFrame& frame) {
  RpcBinaryFrameHeader header = frame.header;
  header.magic = kBinaryMagic;
  header.header_size = static_cast<uint16_t>(kBinaryHeaderSize);
  header.payload_len = static_cast<uint32_t>(frame.payload.size());
  header.payload_crc32 = frame.payload.empty() ? 0 : rpcCrc32(frame.payload.data(), frame.payload.size());

  std::vector<uint8_t> header_bytes = encodeHeader(header, 0);
  header.header_crc32 = rpcCrc32(header_bytes.data(), header_bytes.size());
  header_bytes = encodeHeader(header, header.header_crc32);

  std::vector<uint8_t> out;
  out.reserve(header_bytes.size() + frame.payload.size());
  out.insert(out.end(), header_bytes.begin(), header_bytes.end());
  out.insert(out.end(), frame.payload.begin(), frame.payload.end());
  return out;
}

RpcBinaryFrame decodeBinaryFrame(const uint8_t* data, size_t size) {
  if (data == nullptr) throw RpcFrameError("binary frame data is null");
  if (size < kBinaryHeaderSize) throw RpcFrameError("binary frame is too small");

  RpcBinaryFrame frame;
  frame.header = decodeHeader(data);
  if (frame.header.magic != kBinaryMagic) throw RpcFrameError("binary frame magic mismatch");
  if (frame.header.header_size != kBinaryHeaderSize) throw RpcFrameError("unsupported binary frame header size");
  if (size != static_cast<size_t>(frame.header.header_size) + frame.header.payload_len) {
    throw RpcFrameError("binary frame size mismatch");
  }

  std::vector<uint8_t> header_bytes(data, data + kBinaryHeaderSize);
  std::memset(header_bytes.data() + 40, 0, sizeof(uint32_t));
  const uint32_t expected_header_crc = rpcCrc32(header_bytes.data(), header_bytes.size());
  if (expected_header_crc != frame.header.header_crc32) throw RpcFrameError("binary frame header CRC mismatch");

  frame.payload.assign(data + kBinaryHeaderSize, data + size);
  const uint32_t payload_crc = frame.payload.empty() ? 0 : rpcCrc32(frame.payload.data(), frame.payload.size());
  if (payload_crc != frame.header.payload_crc32) throw RpcFrameError("binary frame payload CRC mismatch");
  return frame;
}

RpcBinaryFrame readBinaryFrame(const RpcReadByte& read_byte, size_t max_payload_size) {
  return readBinaryFrameWithPrefix({}, read_byte, max_payload_size);
}

RpcBinaryFrame readBinaryFrameWithPrefix(const std::vector<uint8_t>& prefix, const RpcReadByte& read_byte, size_t max_payload_size) {
  std::vector<uint8_t> header = prefix;
  header.reserve(kBinaryHeaderSize);
  while (header.size() < kBinaryHeaderSize) header.push_back(static_cast<uint8_t>(read_byte()));

  const auto decoded_header = decodeHeader(header.data());
  if (decoded_header.magic != kBinaryMagic) throw RpcFrameError("binary frame magic mismatch");
  if (decoded_header.header_size != kBinaryHeaderSize) throw RpcFrameError("unsupported binary frame header size");
  if (decoded_header.payload_len > max_payload_size) throw RpcFrameError("binary frame payload is too large");

  std::vector<uint8_t> frame_bytes = header;
  frame_bytes.reserve(kBinaryHeaderSize + decoded_header.payload_len);
  while (frame_bytes.size() < kBinaryHeaderSize + decoded_header.payload_len) {
    frame_bytes.push_back(static_cast<uint8_t>(read_byte()));
  }
  return decodeBinaryFrame(frame_bytes.data(), frame_bytes.size());
}

void writeBinaryFrame(const RpcWriteBytes& write_bytes, const RpcBinaryFrame& frame) {
  const auto encoded = encodeBinaryFrame(frame);
  write_bytes(encoded.data(), encoded.size());
}

} // namespace audio_studio::rpc
