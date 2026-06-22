#pragma once

#include <cstdint>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "status.hpp"

namespace audio_studio::drivers::audio {

using AudioResult = framework::Status;
using AudioFrame = std::vector<uint8_t>;

struct AudioOpenParams {
  std::string device_name;
  bool blocking_write = true;
  std::map<std::string, std::string> options;
};

struct AudioStreamParams {
  uint32_t sample_rate = 48000;
  uint16_t channels = 2;
  uint16_t bytes_per_sample = 2;
};

struct AudioStreamStats {
  size_t frames_written = 0;
  size_t frames_read = 0;
  bool running = false;
};

struct AudioDeviceCaps {
  uint32_t max_sample_rate = 192000;
  uint16_t max_channels = 8;
  bool playback = true;
  bool capture = true;
};

class IAudioPlaybackDevice {
public:
  virtual ~IAudioPlaybackDevice() = default;

  virtual AudioResult open(const AudioOpenParams& params) = 0;
  virtual AudioResult prepare(const AudioStreamParams& params) = 0;
  virtual AudioResult start() = 0;
  virtual AudioResult writeFrame(const AudioFrame& frame, uint32_t timeout_ms) = 0;
  virtual AudioResult drain() = 0;
  virtual AudioResult stop() = 0;
  virtual void close() = 0;
  virtual AudioStreamStats getStats() const = 0;
  virtual AudioDeviceCaps getCaps() const = 0;
};

class IAudioCaptureDevice {
public:
  virtual ~IAudioCaptureDevice() = default;

  virtual AudioResult open(const AudioOpenParams& params) = 0;
  virtual AudioResult prepare(const AudioStreamParams& params) = 0;
  virtual AudioResult start() = 0;
  virtual AudioResult readFrame(AudioFrame& frame, uint32_t timeout_ms) = 0;
  virtual AudioResult stop() = 0;
  virtual void close() = 0;
  virtual AudioStreamStats getStats() const = 0;
  virtual AudioDeviceCaps getCaps() const = 0;
};

class IAudioPlaybackDeviceFactory {
public:
  virtual ~IAudioPlaybackDeviceFactory() = default;
  virtual std::string name() const = 0;
  virtual AudioResult create(const AudioOpenParams& params, std::unique_ptr<IAudioPlaybackDevice>& out) const = 0;
};

class IAudioCaptureDeviceFactory {
public:
  virtual ~IAudioCaptureDeviceFactory() = default;
  virtual std::string name() const = 0;
  virtual AudioResult create(const AudioOpenParams& params, std::unique_ptr<IAudioCaptureDevice>& out) const = 0;
};

class AudioDeviceRegistry {
public:
  static AudioDeviceRegistry& instance() {
    static AudioDeviceRegistry registry;
    return registry;
  }

  AudioResult registerPlaybackFactory(std::unique_ptr<IAudioPlaybackDeviceFactory> factory) {
    if (!factory) return AudioResult::invalidArgument("audio playback factory is null");
    const auto factory_name = factory->name();
    if (factory_name.empty()) return AudioResult::invalidArgument("audio playback factory name is empty");
    if (playback_factories_.find(factory_name) != playback_factories_.end()) {
      return AudioResult::invalidArgument("audio playback factory already registered: " + factory_name);
    }
    playback_factories_.emplace(factory_name, std::move(factory));
    return AudioResult::success();
  }

  AudioResult registerCaptureFactory(std::unique_ptr<IAudioCaptureDeviceFactory> factory) {
    if (!factory) return AudioResult::invalidArgument("audio capture factory is null");
    const auto factory_name = factory->name();
    if (factory_name.empty()) return AudioResult::invalidArgument("audio capture factory name is empty");
    if (capture_factories_.find(factory_name) != capture_factories_.end()) {
      return AudioResult::invalidArgument("audio capture factory already registered: " + factory_name);
    }
    capture_factories_.emplace(factory_name, std::move(factory));
    return AudioResult::success();
  }

  bool hasPlaybackFactory(const std::string& name) const {
    return playback_factories_.find(name) != playback_factories_.end();
  }

  bool hasCaptureFactory(const std::string& name) const {
    return capture_factories_.find(name) != capture_factories_.end();
  }

  AudioResult createPlayback(const std::string& name,
                             const AudioOpenParams& params,
                             std::unique_ptr<IAudioPlaybackDevice>& out) const {
    out.reset();
    const auto it = playback_factories_.find(name);
    if (it == playback_factories_.end()) return AudioResult::unavailable("audio playback factory not registered: " + name);
    return it->second->create(params, out);
  }

  AudioResult createCapture(const std::string& name,
                            const AudioOpenParams& params,
                            std::unique_ptr<IAudioCaptureDevice>& out) const {
    out.reset();
    const auto it = capture_factories_.find(name);
    if (it == capture_factories_.end()) return AudioResult::unavailable("audio capture factory not registered: " + name);
    return it->second->create(params, out);
  }

  std::vector<std::string> playbackFactoryNames() const {
    std::vector<std::string> names;
    names.reserve(playback_factories_.size());
    for (const auto& item : playback_factories_) names.push_back(item.first);
    return names;
  }

  std::vector<std::string> captureFactoryNames() const {
    std::vector<std::string> names;
    names.reserve(capture_factories_.size());
    for (const auto& item : capture_factories_) names.push_back(item.first);
    return names;
  }

  void clear() {
    playback_factories_.clear();
    capture_factories_.clear();
  }

private:
  std::map<std::string, std::unique_ptr<IAudioPlaybackDeviceFactory>> playback_factories_;
  std::map<std::string, std::unique_ptr<IAudioCaptureDeviceFactory>> capture_factories_;
};

} // namespace audio_studio::drivers::audio
