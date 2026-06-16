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

class IDummyDriver {
public:
  virtual ~IDummyDriver() = default;
  virtual framework::Status open() = 0;
  virtual framework::Status close() = 0;
  virtual framework::Status start() = 0;
  virtual framework::Status stop() = 0;
  virtual framework::Status sendCommand(const std::string& command) = 0;
  virtual DummyTelemetry telemetry() const = 0;
};

class DummyDriver final : public IDummyDriver {
public:
  framework::Status open() override;
  framework::Status close() override;
  framework::Status start() override;
  framework::Status stop() override;
  framework::Status sendCommand(const std::string& command) override;
  DummyTelemetry telemetry() const override;
  std::vector<std::string> commandLog() const;

private:
  bool opened_ = false;
  bool running_ = false;
  int frames_processed_ = 0;
  std::vector<std::string> commands_;
};

} // namespace audio_studio::drivers::dummy
