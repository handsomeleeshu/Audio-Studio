#pragma once

#include <string>

#include "audio_device.hpp"

namespace audio_studio::drivers::audio {

class LinuxHostAudioPlaybackDevice final : public IAudioPlaybackDevice {
public:
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
  bool open_ = false;
  bool prepared_ = false;
  bool running_ = false;
  size_t frames_written_ = 0;
};

class LinuxHostAudioCaptureDevice final : public IAudioCaptureDevice {
public:
  AudioResult open(const AudioOpenParams& params) override;
  AudioResult prepare(const AudioStreamParams& params) override;
  AudioResult start() override;
  AudioResult readFrame(AudioFrame& frame, uint32_t timeout_ms) override;
  AudioResult stop() override;
  void close() override;
  AudioStreamStats getStats() const override;
  AudioDeviceCaps getCaps() const override;

  AudioResult injectCaptureFrame(AudioFrame frame);

private:
  std::string device_name_;
  AudioStreamParams params_;
  bool open_ = false;
  bool prepared_ = false;
  bool running_ = false;
  size_t frames_read_ = 0;
  std::vector<AudioFrame> capture_frames_;
};

class LinuxHostAudioPlaybackDeviceFactory final : public IAudioPlaybackDeviceFactory {
public:
  std::string name() const override { return "linux-host"; }
  std::unique_ptr<IAudioPlaybackDevice> create(const AudioOpenParams& params) const override {
    auto device = std::make_unique<LinuxHostAudioPlaybackDevice>();
    if (!device->open(params).ok()) return nullptr;
    return device;
  }
};

class LinuxHostAudioCaptureDeviceFactory final : public IAudioCaptureDeviceFactory {
public:
  std::string name() const override { return "linux-host"; }
  std::unique_ptr<IAudioCaptureDevice> create(const AudioOpenParams& params) const override {
    auto device = std::make_unique<LinuxHostAudioCaptureDevice>();
    if (!device->open(params).ok()) return nullptr;
    return device;
  }
};

} // namespace audio_studio::drivers::audio
