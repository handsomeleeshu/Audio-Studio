#pragma once

#include <cstdint>
#include <vector>

namespace audio_studio::framework::transport {

constexpr uint32_t kTransportFrameRequest = 1u << 0u;
constexpr uint32_t kTransportFrameResponse = 1u << 1u;
constexpr uint32_t kTransportFrameAck = 1u << 2u;
constexpr uint32_t kTransportFrameAckRequired = 1u << 3u;
constexpr uint32_t kTransportFrameAsync = 1u << 4u;
constexpr uint32_t kTransportFrameOpen = 1u << 5u;
constexpr uint32_t kTransportFrameClose = 1u << 6u;
constexpr uint32_t kTransportFrameError = 1u << 7u;

struct TransportFrame {
  uint16_t version = 1;
  uint16_t service_id = 0;
  uint16_t channel_id = 0;
  uint16_t command_id = 0;
  uint32_t flags = 0;
  uint32_t sequence_id = 0;
  uint32_t session_id = 0;
  std::vector<uint8_t> payload;
};

} // namespace audio_studio::framework::transport
