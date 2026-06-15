#pragma once

#include <cstdint>
#include <vector>

namespace audio_studio::framework::transport {

struct TransportFrame {
  uint8_t version = 1;
  uint16_t service_id = 0;
  uint16_t channel_id = 0;
  uint32_t sequence_id = 0;
  std::vector<uint8_t> payload;
};

} // namespace audio_studio::framework::transport
