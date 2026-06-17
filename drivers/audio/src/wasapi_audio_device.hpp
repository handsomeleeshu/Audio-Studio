#pragma once

#include <cstddef>
#include <string>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <windows.h>

#include "audio_device.hpp"

namespace audio_studio::drivers::audio {

class WasapiAudioPlaybackDevice final : public IAudioPlaybackDevice {
public:
  ~WasapiAudioPlaybackDevice() override;

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
  AudioResult ensureStarted();
  void releaseClient();

  std::string device_name_;
  AudioStreamParams params_;
  IAudioClient* audio_client_ = nullptr;
  IAudioRenderClient* render_client_ = nullptr;
  UINT32 buffer_frames_ = 0;
  bool prepared_ = false;
  bool running_ = false;
  bool client_started_ = false;
  bool blocking_write_ = true;
  size_t frames_written_ = 0;
  size_t frame_bytes_ = 0;
};

class WasapiAudioCaptureDevice final : public IAudioCaptureDevice {
public:
  ~WasapiAudioCaptureDevice() override;

  AudioResult open(const AudioOpenParams& params) override;
  AudioResult prepare(const AudioStreamParams& params) override;
  AudioResult start() override;
  AudioResult readFrame(AudioFrame& frame, uint32_t timeout_ms) override;
  AudioResult stop() override;
  void close() override;
  AudioStreamStats getStats() const override;
  AudioDeviceCaps getCaps() const override;

private:
  AudioResult ensureStarted();
  void releaseClient();

  std::string device_name_;
  AudioStreamParams params_;
  IAudioClient* audio_client_ = nullptr;
  IAudioCaptureClient* capture_client_ = nullptr;
  UINT32 buffer_frames_ = 0;
  bool prepared_ = false;
  bool running_ = false;
  bool client_started_ = false;
  size_t frames_read_ = 0;
  size_t frame_bytes_ = 0;
};

} // namespace audio_studio::drivers::audio
