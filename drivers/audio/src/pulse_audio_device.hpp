#pragma once

#include <cstddef>
#include <string>

#include <pulse/simple.h>

#include "audio_device.hpp"

namespace audio_studio::drivers::audio {

class PulseAudioPlaybackDevice final : public IAudioPlaybackDevice {
public:
  ~PulseAudioPlaybackDevice() override;

  AudioResult open(const AudioOpenParams& params) override;
  AudioResult prepare(const AudioStreamParams& params) override;
  AudioResult start() override;
  AudioResult writeFrame(const AudioFrame& frame, uint32_t timeout_ms) override;
  AudioResult drain() override;
  AudioResult stop() override;
  void close() override;
  AudioStreamStats getStats() const override;
  AudioDeviceCaps getCaps() const override;

private:
  std::string device_name_;
  AudioStreamParams params_;
  pa_simple* stream_ = nullptr;
  bool prepared_ = false;
  bool running_ = false;
  bool blocking_write_ = true;
  size_t frames_written_ = 0;
  size_t frame_bytes_ = 0;
};

class PulseAudioCaptureDevice final : public IAudioCaptureDevice {
public:
  ~PulseAudioCaptureDevice() override;

  AudioResult open(const AudioOpenParams& params) override;
  AudioResult prepare(const AudioStreamParams& params) override;
  AudioResult start() override;
  AudioResult readFrame(AudioFrame& frame, uint32_t timeout_ms) override;
  AudioResult stop() override;
  void close() override;
  AudioStreamStats getStats() const override;
  AudioDeviceCaps getCaps() const override;

private:
  std::string device_name_;
  AudioStreamParams params_;
  pa_simple* stream_ = nullptr;
  bool prepared_ = false;
  bool running_ = false;
  size_t frames_read_ = 0;
  size_t frame_bytes_ = 0;
};

} // namespace audio_studio::drivers::audio
