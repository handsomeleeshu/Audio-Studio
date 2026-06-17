#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "status.hpp"
#include "transport_frame.hpp"

namespace audio_studio::framework::transport {

class FrameCodec {
public:
  static std::vector<uint8_t> encode(const TransportFrame& frame);
  static framework::Status decode(const uint8_t* data, size_t size, TransportFrame& out);
};

} // namespace audio_studio::framework::transport
