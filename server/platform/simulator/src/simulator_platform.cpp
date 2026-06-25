#include "simulator_platform.hpp"

namespace audio_studio::platform::simulator {

core::PlatformProfile makeSimulatorPlatformProfile() {
  return {
    "simulator",
    "Audio Controller Simulator",
    "simulator-pipe",
    {"audio-controller", "log", "transport", "data-link"},
    true,
  };
}

} // namespace audio_studio::platform::simulator
