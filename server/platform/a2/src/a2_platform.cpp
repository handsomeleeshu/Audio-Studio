#include "a2_platform.hpp"

namespace audio_studio::platform::a2 {

core::PlatformProfile makeA2PlatformProfile() {
  return {
    "a2",
    "A2 Platform",
    "a2-physical-or-controller",
    {"audio", "control", "log", "dump", "transport"},
    false,
  };
}

} // namespace audio_studio::platform::a2
