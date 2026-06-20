#include "datalink_frame.hpp"

#include <algorithm>

#include "transport_checksum.hpp"

namespace audio_studio::framework::transport {
namespace {

constexpr uint32_t kMagic = 0x4c445341u; // "ASDL" little-endian

void appendU8(std::vector<uint8_t>& out, uint8_t value) {
  out.push_back(value);
}

void appendU16(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
}

void appendU32(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
}

uint16_t readU16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8u);
}

uint32_t readU32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8u) |
         (static_cast<uint32_t>(data[2]) << 16u) |
         (static_cast<uint32_t>(data[3]) << 24u);
}

} // namespace

std::vector<uint8_t> DataLinkFrameCodec::encode(const DataLinkFrame& frame) {
  std::vector<uint8_t> out;
  out.reserve(kHeaderSize + frame.payload.size());
  appendU32(out, kMagic);
  appendU8(out, frame.version);
  appendU8(out, static_cast<uint8_t>(kHeaderSize));
  appendU16(out, frame.flags);
  appendU32(out, frame.link_sequence);
  appendU32(out, frame.transport_size);
  appendU32(out, frame.fragment_offset);
  appendU32(out, static_cast<uint32_t>(frame.payload.size()));
  appendU16(out, frame.fragment_index);
  appendU16(out, frame.fragment_count);
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

framework::Status DataLinkFrameCodec::decode(const uint8_t* data, size_t size, DataLinkFrame& out) {
  if (data == nullptr) return framework::Status::invalidArgument("data-link frame data is null");
  if (size < kHeaderSize) return framework::Status::invalidArgument("data-link frame is too small");
  if (readU32(data) != kMagic) return framework::Status::invalidArgument("data-link magic mismatch");
  if (data[5] != kHeaderSize) return framework::Status::invalidArgument("data-link header size mismatch");

  const uint32_t payload_size = readU32(data + 20);
  if (size != kHeaderSize + payload_size) return framework::Status::invalidArgument("data-link size mismatch");

  std::vector<uint8_t> header(data, data + kHeaderSize);
  const uint32_t expected_header_crc = readU32(data + 32);
  std::fill(header.end() - 4, header.end(), 0);
  if (crc32(header.data(), header.size()) != expected_header_crc) {
    return framework::Status::invalidArgument("data-link header crc mismatch");
  }

  const uint32_t expected_payload_crc = readU32(data + 28);
  if (crc32(data + kHeaderSize, payload_size) != expected_payload_crc) {
    return framework::Status::invalidArgument("data-link payload crc mismatch");
  }

  out.version = data[4];
  out.flags = readU16(data + 6);
  out.link_sequence = readU32(data + 8);
  out.transport_size = readU32(data + 12);
  out.fragment_offset = readU32(data + 16);
  out.fragment_index = readU16(data + 24);
  out.fragment_count = readU16(data + 26);
  out.payload.assign(data + kHeaderSize, data + size);
  return framework::Status::success();
}

framework::Status DataLinkFrameCodec::frameSize(const uint8_t* data, size_t size, size_t& frame_size) {
  frame_size = 0;
  if (size < kHeaderSize) return framework::Status::unavailable("data-link frame header is incomplete");
  if (data == nullptr) return framework::Status::invalidArgument("data-link frame data is null");
  if (readU32(data) != kMagic) return framework::Status::invalidArgument("data-link magic mismatch");
  if (data[5] != kHeaderSize) return framework::Status::invalidArgument("data-link header size mismatch");
  frame_size = kHeaderSize + readU32(data + 20);
  if (size < frame_size) return framework::Status::unavailable("data-link frame payload is incomplete");
  return framework::Status::success();
}

DataLinkFrame DataLinkFrameCodec::makeAck(const DataLinkFrame& frame) {
  DataLinkFrame ack;
  ack.flags = kDataLinkFrameAck;
  ack.link_sequence = frame.link_sequence;
  ack.transport_size = frame.transport_size;
  ack.fragment_offset = frame.fragment_offset;
  ack.fragment_index = frame.fragment_index;
  ack.fragment_count = frame.fragment_count;
  return ack;
}

DataLinkFrame DataLinkFrameCodec::makeNak(const DataLinkFrame& frame) {
  auto nak = makeAck(frame);
  nak.flags = kDataLinkFrameNak;
  return nak;
}

} // namespace audio_studio::framework::transport
