#pragma once

#include <cstdint>
#include <vector>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::drivers::audio {

enum class AudioDirection {
  kPlayback,
  kCapture,
};

struct AudioFormat {
  uint32_t sample_rate = 48000;
  uint16_t channels = 2;
  uint16_t bytes_per_sample = 2;
};

struct AudioStats {
  size_t frames_written = 0;
  size_t frames_read = 0;
  bool running = false;
};

class AudioDevice {
public:
  framework::Status open(AudioDirection direction, AudioFormat format);
  framework::Status start();
  framework::Status writeFrame(const std::vector<uint8_t>& frame);
  framework::Status injectCaptureFrame(const std::vector<uint8_t>& frame);
  framework::Status readFrame(std::vector<uint8_t>& out);
  framework::Status stop();
  void close();
  AudioStats stats() const;
  bool isOpen() const;

private:
  AudioDirection direction_ = AudioDirection::kPlayback;
  AudioFormat format_;
  bool open_ = false;
  bool running_ = false;
  size_t frames_written_ = 0;
  size_t frames_read_ = 0;
  std::vector<std::vector<uint8_t>> capture_frames_;
};

} // namespace audio_studio::drivers::audio
