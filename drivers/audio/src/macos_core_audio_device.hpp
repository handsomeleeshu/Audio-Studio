#pragma once

#include "audio_device.hpp"

#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioToolbox.h>

namespace audio_studio::drivers::audio {

/// macOS Core Audio playback device implementation using AudioQueue
class MacOsCoreAudioPlaybackDevice final : public IAudioPlaybackDevice {
public:
  ~MacOsCoreAudioPlaybackDevice() override;

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
  AudioResult configureAudioQueue();
  static void audioQueueOutputCallback(void* user_data, AudioQueueRef queue, AudioQueueBufferRef buffer);

  std::string device_name_;
  AudioStreamParams params_{};
  AudioQueueRef audio_queue_ = nullptr;
  AudioStreamBasicDescription format_{};
  bool prepared_ = false;
  bool running_ = false;
  bool blocking_write_ = true;
  size_t frames_written_ = 0;
  size_t frame_bytes_ = 0;
  size_t buffer_index_ = 0;
  static constexpr size_t kBufferCount = 3;
  static constexpr size_t kBufferSizeFrames = 1024;
};

/// macOS Core Audio capture device implementation using AudioQueue
class MacOsCoreAudioCaptureDevice final : public IAudioCaptureDevice {
public:
  ~MacOsCoreAudioCaptureDevice() override;

  AudioResult open(const AudioOpenParams& params) override;
  AudioResult prepare(const AudioStreamParams& params) override;
  AudioResult start() override;
  AudioResult readFrame(AudioFrame& frame, uint32_t timeout_ms) override;
  AudioResult stop() override;
  void close() override;
  AudioStreamStats getStats() const override;
  AudioDeviceCaps getCaps() const override;

private:
  AudioResult configureAudioQueue();
  static void audioQueueInputCallback(void* user_data,
                                       AudioQueueRef queue,
                                       AudioQueueBufferRef buffer,
                                       const AudioTimeStamp* start_time,
                                       UInt32 num_packets,
                                       const AudioStreamPacketDescription* packet_desc);

  std::string device_name_;
  AudioStreamParams params_{};
  AudioQueueRef audio_queue_ = nullptr;
  AudioStreamBasicDescription format_{};
  bool prepared_ = false;
  bool running_ = false;
  size_t frames_read_ = 0;
  size_t frame_bytes_ = 0;

  // Capture buffer management
  std::vector<uint8_t> capture_buffer_;
  size_t capture_read_pos_ = 0;
  size_t capture_write_pos_ = 0;
  static constexpr size_t kCaptureBufferSize = 65536;  // 64KB ring buffer
};

} // namespace audio_studio::drivers::audio
