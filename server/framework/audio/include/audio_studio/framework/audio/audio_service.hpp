#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "audio_studio/framework/status.hpp"
#include "audio_device.hpp"

namespace audio_studio::framework::audio {

enum class StreamDirection {
  kPlayback,
  kCapture,
};

struct AudioStream {
  std::string id;
  uint32_t numeric_session_id = 0;
  uint32_t numeric_stream_id = 0;
  StreamDirection direction = StreamDirection::kPlayback;
  std::string driver_factory = "linux-host";
  std::string device_name = "default";
  int sample_rate = 48000;
  int channels = 2;
  int bytes_per_sample = 2;
  bool prepared = false;
  bool running = false;
};

struct AudioServiceConfig {
  std::string driver_factory = "linux-host";
  std::string default_device_name = "default";
};

struct AudioIoStats {
  size_t frames_written = 0;
  size_t frames_read = 0;
  bool running = false;
};

class AudioService {
public:
  AudioService();
  ~AudioService();

  void configureDeviceRegistry(drivers::audio::AudioDeviceRegistry* registry, AudioServiceConfig config = {});
  framework::Status create(AudioStream stream);
  framework::Status prepare(const std::string& id);
  framework::Status start(const std::string& id);
  framework::Status drain(const std::string& id);
  framework::Status stop(const std::string& id);
  framework::Status remove(const std::string& id);
  framework::Status get(const std::string& id, AudioStream& out) const;
  framework::Status getByNumericSession(uint32_t numeric_session_id, AudioStream& out) const;
  framework::Status writeFrames(uint32_t numeric_session_id, const std::vector<uint8_t>& data, uint32_t timeout_ms, size_t& accepted_bytes);
  framework::Status readFrames(uint32_t numeric_session_id, size_t max_bytes, uint32_t timeout_ms, std::vector<uint8_t>& data);
  framework::Status getStats(const std::string& id, AudioIoStats& out) const;
  std::vector<AudioStream> list() const;

private:
  struct SessionState {
    AudioStream stream;
    std::unique_ptr<drivers::audio::IAudioPlaybackDevice> playback;
    std::unique_ptr<drivers::audio::IAudioCaptureDevice> capture;
  };

  framework::Status prepare(SessionState& state);
  framework::Status requirePlayback(const SessionState& state) const;
  framework::Status requireCapture(const SessionState& state) const;
  drivers::audio::AudioStreamParams streamParams(const AudioStream& stream) const;
  std::string effectiveDeviceName(const AudioStream& stream) const;
  std::string effectiveFactoryName(const AudioStream& stream) const;

  drivers::audio::AudioDeviceRegistry* registry_ = nullptr;
  AudioServiceConfig config_;
  std::map<std::string, SessionState> sessions_;
  std::map<uint32_t, std::string> numeric_session_ids_;
};

} // namespace audio_studio::framework::audio
