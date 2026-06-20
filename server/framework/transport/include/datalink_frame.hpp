#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "status.hpp"

namespace audio_studio::framework::transport {

constexpr uint16_t kDataLinkFrameData = 1u << 0u;
constexpr uint16_t kDataLinkFrameAck = 1u << 1u;
constexpr uint16_t kDataLinkFrameNak = 1u << 2u;
constexpr uint16_t kDataLinkFrameEnd = 1u << 3u;

struct DataLinkFrame {
  uint8_t version = 1;
  uint16_t flags = 0;
  uint32_t link_sequence = 0;
  uint32_t transport_size = 0;
  uint32_t fragment_offset = 0;
  uint16_t fragment_index = 0;
  uint16_t fragment_count = 0;
  std::vector<uint8_t> payload;
};

class DataLinkFrameCodec {
public:
  static constexpr size_t kHeaderSize = 36;

  static std::vector<uint8_t> encode(const DataLinkFrame& frame);
  static framework::Status decode(const uint8_t* data, size_t size, DataLinkFrame& out);
  static framework::Status frameSize(const uint8_t* data, size_t size, size_t& frame_size);
  static DataLinkFrame makeAck(const DataLinkFrame& frame);
  static DataLinkFrame makeNak(const DataLinkFrame& frame);
};

} // namespace audio_studio::framework::transport
