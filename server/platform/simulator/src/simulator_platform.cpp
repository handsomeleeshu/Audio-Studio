#include "simulator_platform.hpp"

namespace audio_studio::platform::simulator {

core::PlatformProfile makeSimulatorPlatformProfile() {
  return {
    "rv32qemu",
    "rv32qemu Simulator",
    "simulator-pipe",
    {"audio-controller", "log", "transport", "data-link"},
    true,
  };
}

} // namespace audio_studio::platform::simulator
