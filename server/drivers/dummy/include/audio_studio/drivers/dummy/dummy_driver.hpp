#pragma once

#include <string>
#include <vector>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::drivers::dummy {

struct DummyTelemetry {
  int frames_processed = 0;
  int commands_processed = 0;
  bool running = false;
};

class DummyDriver {
public:
  framework::Status open();
  framework::Status close();
  framework::Status start();
  framework::Status stop();
  framework::Status sendCommand(const std::string& command);
  DummyTelemetry telemetry() const;
  std::vector<std::string> commandLog() const;

private:
  bool opened_ = false;
  bool running_ = false;
  int frames_processed_ = 0;
  std::vector<std::string> commands_;
};

} // namespace audio_studio::drivers::dummy
