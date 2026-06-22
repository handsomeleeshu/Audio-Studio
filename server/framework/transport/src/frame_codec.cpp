#include "frame_codec.hpp"

#include <algorithm>

#include "transport_checksum.hpp"

namespace audio_studio::framework::transport {
namespace {

constexpr uint32_t kMagic = 0x4d545341u; // "ASTM" little-endian
constexpr size_t kHeaderSize = 36;

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

} // namespace

std::vector<uint8_t> FrameCodec::encode(const TransportFrame& frame) {
  std::vector<uint8_t> out;
  out.reserve(kHeaderSize + frame.payload.size());
  appendU32(out, kMagic);
  appendU16(out, frame.version);
  appendU16(out, static_cast<uint16_t>(kHeaderSize));
  appendU16(out, frame.channel_id);
  appendU16(out, frame.command_id);
  appendU32(out, frame.flags);
  appendU32(out, frame.sequence_id);
  appendU32(out, frame.session_id);
  appendU32(out, static_cast<uint32_t>(frame.payload.size()));
  appendU32(out, crc32(frame.payload.data(), frame.payload.size()));
  appendU32(out, 0);
  const auto header_crc = crc32(out.data(), out.size());
  out[out.size() - 4] = static_cast<uint8_t>(header_crc & 0xffu);
  out[out.size() - 3] = static_cast<uint8_t>((header_crc >> 8u) & 0xffu);
  out[out.size() - 2] = static_cast<uint8_t>((header_crc >> 16u) & 0xffu);
  out[out.size() - 1] = static_cast<uint8_t>((header_crc >> 24u) & 0xffu);
  out.insert(out.end(), frame.payload.begin(), frame.payload.end());
  return out;
}

framework::Status FrameCodec::decode(const uint8_t* data, size_t size, TransportFrame& out) {
  if (data == nullptr) return framework::Status::invalidArgument("transport frame data is null");
  if (size < kHeaderSize) return framework::Status::invalidArgument("transport frame is too small");
  if (readU32(data) != kMagic) return framework::Status::invalidArgument("transport frame magic mismatch");
  if (readU16(data + 6) != kHeaderSize) return framework::Status::invalidArgument("transport frame header size mismatch");
  const auto payload_size = readU32(data + 24);
  if (size != kHeaderSize + payload_size) return framework::Status::invalidArgument("transport frame size mismatch");

  std::vector<uint8_t> header(data, data + kHeaderSize);
  const uint32_t expected_header_crc = readU32(data + 32);
  std::fill(header.end() - 4, header.end(), 0);
  if (crc32(header.data(), header.size()) != expected_header_crc) {
    return framework::Status::invalidArgument("transport frame header crc mismatch");
  }
  const uint32_t expected_payload_crc = readU32(data + 28);
  if (crc32(data + kHeaderSize, payload_size) != expected_payload_crc) {
    return framework::Status::invalidArgument("transport frame payload crc mismatch");
  }

  out.version = readU16(data + 4);
  out.service_id = readU16(data + 8);
  out.channel_id = readU16(data + 8);
  out.command_id = readU16(data + 10);
  out.flags = readU32(data + 12);
  out.sequence_id = readU32(data + 16);
  out.session_id = readU32(data + 20);
  out.payload.assign(data + kHeaderSize, data + size);
  return framework::Status::success();
}

} // namespace audio_studio::framework::transport
