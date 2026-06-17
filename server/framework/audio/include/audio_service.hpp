#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "status.hpp"
#include "audio_device.hpp"

namespace audio_studio::framework::audio {

enum class StreamDirection {
  kPlayback,
  kCapture,
};

struct AudioStream {
  std::string id;
  StreamDirection direction = StreamDirection::kPlayback;
  std::string driver_factory = "linux-host";
  std::string device_name = "default";
  int sample_rate = 48000;
  int channels = 2;
  int bytes_per_sample = 2;
  bool blocking_write = true;
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

class AudioPlaybackSession {
public:
  AudioPlaybackSession(AudioStream stream, std::unique_ptr<drivers::audio::IAudioPlaybackDevice> device);
  ~AudioPlaybackSession();

  framework::Status prepare();
  framework::Status start();
  framework::Status writeFrames(const std::vector<uint8_t>& data, uint32_t timeout_ms, size_t& accepted_bytes);
  framework::Status drain();
  framework::Status stop();
  framework::Status close();
  framework::Status snapshot(AudioStream& out) const;
  framework::Status getStats(AudioIoStats& out) const;
  const std::string& id() const;

private:
  drivers::audio::AudioStreamParams streamParamsLocked() const;

  mutable std::mutex mutex_;
  AudioStream stream_;
  std::unique_ptr<drivers::audio::IAudioPlaybackDevice> device_;
  bool closed_ = false;
};

class AudioCaptureSession {
public:
  AudioCaptureSession(AudioStream stream, std::unique_ptr<drivers::audio::IAudioCaptureDevice> device);
  ~AudioCaptureSession();

  framework::Status prepare();
  framework::Status start();
  framework::Status readFrames(size_t max_bytes, uint32_t timeout_ms, std::vector<uint8_t>& data);
  framework::Status stop();
  framework::Status close();
  framework::Status snapshot(AudioStream& out) const;
  framework::Status getStats(AudioIoStats& out) const;
  const std::string& id() const;

private:
  drivers::audio::AudioStreamParams streamParamsLocked() const;

  mutable std::mutex mutex_;
  AudioStream stream_;
  std::unique_ptr<drivers::audio::IAudioCaptureDevice> device_;
  bool closed_ = false;
};

class AudioService {
public:
  AudioService();
  ~AudioService();

  void configureDeviceRegistry(drivers::audio::AudioDeviceRegistry* registry, AudioServiceConfig config = {});
  framework::Status createPlaybackSession(AudioStream stream, std::shared_ptr<AudioPlaybackSession>& out);
  framework::Status createCaptureSession(AudioStream stream, std::shared_ptr<AudioCaptureSession>& out);
  framework::Status create(AudioStream stream);
  framework::Status prepare(const std::string& id);
  framework::Status start(const std::string& id);
  framework::Status drain(const std::string& id);
  framework::Status stop(const std::string& id);
  framework::Status remove(const std::string& id);
  framework::Status get(const std::string& id, AudioStream& out) const;
  framework::Status writeFrames(const std::string& id, const std::vector<uint8_t>& data, uint32_t timeout_ms, size_t& accepted_bytes);
  framework::Status readFrames(const std::string& id, size_t max_bytes, uint32_t timeout_ms, std::vector<uint8_t>& data);
  framework::Status getStats(const std::string& id, AudioIoStats& out) const;
  std::vector<AudioStream> list() const;
  std::shared_ptr<AudioPlaybackSession> playbackSession(const std::string& id) const;
  std::shared_ptr<AudioCaptureSession> captureSession(const std::string& id) const;

private:
  std::string effectiveDeviceName(const AudioStream& stream) const;
  std::string effectiveFactoryName(const AudioStream& stream) const;
  framework::Status validateStream(const AudioStream& stream) const;
  bool hasSessionLocked(const std::string& id) const;

  drivers::audio::AudioDeviceRegistry* registry_ = nullptr;
  AudioServiceConfig config_;
  mutable std::mutex sessions_mutex_;
  std::map<std::string, std::shared_ptr<AudioPlaybackSession>> playback_sessions_;
  std::map<std::string, std::shared_ptr<AudioCaptureSession>> capture_sessions_;
};

} // namespace audio_studio::framework::audio
