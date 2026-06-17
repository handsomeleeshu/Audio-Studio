#include "frame_codec.hpp"

#include <algorithm>

namespace audio_studio::framework::transport {
namespace {

constexpr uint8_t kMagic0 = 'A';
constexpr uint8_t kMagic1 = 'S';
constexpr size_t kHeaderSize = 15;

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
  out.push_back(kMagic0);
  out.push_back(kMagic1);
  out.push_back(frame.version);
  appendU16(out, frame.service_id);
  appendU16(out, frame.channel_id);
  appendU32(out, frame.sequence_id);
  appendU32(out, static_cast<uint32_t>(frame.payload.size()));
  out.insert(out.end(), frame.payload.begin(), frame.payload.end());
  return out;
}

framework::Status FrameCodec::decode(const uint8_t* data, size_t size, TransportFrame& out) {
  if (data == nullptr) return framework::Status::invalidArgument("transport frame data is null");
  if (size < kHeaderSize) return framework::Status::invalidArgument("transport frame is too small");
  if (data[0] != kMagic0 || data[1] != kMagic1) return framework::Status::invalidArgument("transport frame magic mismatch");
  const auto payload_size = readU32(data + 11);
  if (size != kHeaderSize + payload_size) return framework::Status::invalidArgument("transport frame size mismatch");

  out.version = data[2];
  out.service_id = readU16(data + 3);
  out.channel_id = readU16(data + 5);
  out.sequence_id = readU32(data + 7);
  out.payload.assign(data + kHeaderSize, data + size);
  return framework::Status::success();
}

} // namespace audio_studio::framework::transport
