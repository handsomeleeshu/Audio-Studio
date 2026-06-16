#include "audio_studio/framework/audio/audio_service.hpp"

#include <algorithm>
#include <utility>

namespace audio_studio::framework::audio {
namespace {

drivers::audio::AudioFrame makeFrame(const std::vector<uint8_t>& data) {
  return drivers::audio::AudioFrame(data.begin(), data.end());
}

} // namespace

AudioService::AudioService() = default;
AudioService::~AudioService() = default;

void AudioService::configureDeviceRegistry(drivers::audio::AudioDeviceRegistry* registry, AudioServiceConfig config) {
  registry_ = registry;
  config_ = std::move(config);
}

framework::Status AudioService::create(AudioStream stream) {
  if (stream.id.empty()) return framework::Status::invalidArgument("audio stream id is empty");
  if (stream.sample_rate <= 0) return framework::Status::invalidArgument("audio sample rate must be positive");
  if (stream.channels <= 0) return framework::Status::invalidArgument("audio channel count must be positive");
  if (stream.bytes_per_sample <= 0) return framework::Status::invalidArgument("audio bytes per sample must be positive");
  if (sessions_.find(stream.id) != sessions_.end()) return framework::Status::invalidArgument("audio stream already exists: " + stream.id);

  SessionState state;
  stream.driver_factory = effectiveFactoryName(stream);
  stream.device_name = effectiveDeviceName(stream);
  state.stream = std::move(stream);

  if (registry_ != nullptr) {
    const drivers::audio::AudioOpenParams open_params {state.stream.device_name};
    if (state.stream.direction == StreamDirection::kPlayback) {
      state.playback = registry_->createPlayback(state.stream.driver_factory, open_params);
      if (!state.playback) {
        return framework::Status::unavailable("failed to create playback audio driver: " + state.stream.driver_factory +
                                              " device=" + state.stream.device_name);
      }
    } else {
      state.capture = registry_->createCapture(state.stream.driver_factory, open_params);
      if (!state.capture) {
        return framework::Status::unavailable("failed to create capture audio driver: " + state.stream.driver_factory +
                                              " device=" + state.stream.device_name);
      }
    }
  }

  if (state.stream.numeric_session_id != 0) numeric_session_ids_[state.stream.numeric_session_id] = state.stream.id;
  sessions_.emplace(state.stream.id, std::move(state));
  return framework::Status::success();
}

framework::Status AudioService::prepare(const std::string& id) {
  auto it = sessions_.find(id);
  if (it == sessions_.end()) return framework::Status::unavailable("audio stream not found: " + id);
  return prepare(it->second);
}

framework::Status AudioService::start(const std::string& id) {
  auto it = sessions_.find(id);
  if (it == sessions_.end()) return framework::Status::unavailable("audio stream not found: " + id);

  auto status = prepare(it->second);
  if (!status.ok()) return status;

  if (it->second.playback) {
    status = it->second.playback->start();
    if (!status.ok()) return status;
  }
  if (it->second.capture) {
    status = it->second.capture->start();
    if (!status.ok()) return status;
  }
  it->second.stream.running = true;
  return framework::Status::success();
}

framework::Status AudioService::drain(const std::string& id) {
  auto it = sessions_.find(id);
  if (it == sessions_.end()) return framework::Status::unavailable("audio stream not found: " + id);
  auto status = requirePlayback(it->second);
  if (!status.ok()) return status;
  if (it->second.playback) return it->second.playback->drain();
  return framework::Status::success();
}

framework::Status AudioService::stop(const std::string& id) {
  auto it = sessions_.find(id);
  if (it == sessions_.end()) return framework::Status::unavailable("audio stream not found: " + id);

  framework::Status status = framework::Status::success();
  if (it->second.playback) status = it->second.playback->stop();
  if (status.ok() && it->second.capture) status = it->second.capture->stop();
  if (!status.ok()) return status;
  it->second.stream.running = false;
  return framework::Status::success();
}

framework::Status AudioService::remove(const std::string& id) {
  auto it = sessions_.find(id);
  if (it == sessions_.end()) return framework::Status::unavailable("audio stream not found: " + id);
  if (it->second.playback) it->second.playback->close();
  if (it->second.capture) it->second.capture->close();
  if (it->second.stream.numeric_session_id != 0) numeric_session_ids_.erase(it->second.stream.numeric_session_id);
  sessions_.erase(it);
  return framework::Status::success();
}

framework::Status AudioService::get(const std::string& id, AudioStream& out) const {
  const auto it = sessions_.find(id);
  if (it == sessions_.end()) return framework::Status::unavailable("audio stream not found: " + id);
  out = it->second.stream;
  return framework::Status::success();
}

framework::Status AudioService::getByNumericSession(uint32_t numeric_session_id, AudioStream& out) const {
  const auto id = numeric_session_ids_.find(numeric_session_id);
  if (id == numeric_session_ids_.end()) {
    return framework::Status::unavailable("audio stream not found for numeric session: " + std::to_string(numeric_session_id));
  }
  return get(id->second, out);
}

framework::Status AudioService::writeFrames(uint32_t numeric_session_id,
                                            const std::vector<uint8_t>& data,
                                            uint32_t timeout_ms,
                                            size_t& accepted_bytes) {
  accepted_bytes = 0;
  const auto id = numeric_session_ids_.find(numeric_session_id);
  if (id == numeric_session_ids_.end()) {
    return framework::Status::unavailable("audio stream not found for numeric session: " + std::to_string(numeric_session_id));
  }
  auto it = sessions_.find(id->second);
  if (it == sessions_.end()) return framework::Status::unavailable("audio stream not found: " + id->second);
  auto status = requirePlayback(it->second);
  if (!status.ok()) return status;
  if (!it->second.stream.running) return framework::Status::unavailable("audio playback stream is not running: " + id->second);
  if (data.empty()) return framework::Status::success();

  if (it->second.playback) {
    auto frame = makeFrame(data);
    status = it->second.playback->writeFrame(frame, timeout_ms);
    if (!status.ok()) return status;
  }
  accepted_bytes = data.size();
  return framework::Status::success();
}

framework::Status AudioService::readFrames(uint32_t numeric_session_id,
                                           size_t max_bytes,
                                           uint32_t timeout_ms,
                                           std::vector<uint8_t>& data) {
  data.clear();
  const auto id = numeric_session_ids_.find(numeric_session_id);
  if (id == numeric_session_ids_.end()) {
    return framework::Status::unavailable("audio stream not found for numeric session: " + std::to_string(numeric_session_id));
  }
  auto it = sessions_.find(id->second);
  if (it == sessions_.end()) return framework::Status::unavailable("audio stream not found: " + id->second);
  auto status = requireCapture(it->second);
  if (!status.ok()) return status;
  if (!it->second.stream.running) return framework::Status::unavailable("audio capture stream is not running: " + id->second);
  if (max_bytes == 0) return framework::Status::invalidArgument("audio read max_bytes is zero");

  if (!it->second.capture) {
    data.assign(max_bytes, 0);
    return framework::Status::success();
  }

  const size_t frame_bytes = static_cast<size_t>(it->second.stream.channels) *
                             static_cast<size_t>(it->second.stream.bytes_per_sample);
  const size_t aligned_max = std::max(frame_bytes, (max_bytes / frame_bytes) * frame_bytes);
  drivers::audio::AudioFrame frame(aligned_max, 0);
  status = it->second.capture->readFrame(frame, timeout_ms);
  if (!status.ok()) return status;
  if (frame.size() > max_bytes) frame.resize((max_bytes / frame_bytes) * frame_bytes);
  data.assign(frame.begin(), frame.end());
  return framework::Status::success();
}

framework::Status AudioService::getStats(const std::string& id, AudioIoStats& out) const {
  const auto it = sessions_.find(id);
  if (it == sessions_.end()) return framework::Status::unavailable("audio stream not found: " + id);
  if (it->second.playback) {
    auto stats = it->second.playback->getStats();
    out = {stats.frames_written, stats.frames_read, stats.running};
    return framework::Status::success();
  }
  if (it->second.capture) {
    auto stats = it->second.capture->getStats();
    out = {stats.frames_written, stats.frames_read, stats.running};
    return framework::Status::success();
  }
  out = {};
  out.running = it->second.stream.running;
  return framework::Status::success();
}

std::vector<AudioStream> AudioService::list() const {
  std::vector<AudioStream> out;
  out.reserve(sessions_.size());
  for (const auto& item : sessions_) out.push_back(item.second.stream);
  return out;
}

framework::Status AudioService::prepare(SessionState& state) {
  if (state.stream.prepared) return framework::Status::success();
  const auto params = streamParams(state.stream);
  framework::Status status = framework::Status::success();
  if (state.playback) status = state.playback->prepare(params);
  if (status.ok() && state.capture) status = state.capture->prepare(params);
  if (!status.ok()) return status;
  state.stream.prepared = true;
  return framework::Status::success();
}

framework::Status AudioService::requirePlayback(const SessionState& state) const {
  if (state.stream.direction != StreamDirection::kPlayback) {
    return framework::Status::invalidArgument("audio operation requires playback stream: " + state.stream.id);
  }
  return framework::Status::success();
}

framework::Status AudioService::requireCapture(const SessionState& state) const {
  if (state.stream.direction != StreamDirection::kCapture) {
    return framework::Status::invalidArgument("audio operation requires capture stream: " + state.stream.id);
  }
  return framework::Status::success();
}

drivers::audio::AudioStreamParams AudioService::streamParams(const AudioStream& stream) const {
  drivers::audio::AudioStreamParams params;
  params.sample_rate = static_cast<uint32_t>(stream.sample_rate);
  params.channels = static_cast<uint16_t>(stream.channels);
  params.bytes_per_sample = static_cast<uint16_t>(stream.bytes_per_sample);
  return params;
}

std::string AudioService::effectiveDeviceName(const AudioStream& stream) const {
  if (!stream.device_name.empty()) return stream.device_name;
  return config_.default_device_name.empty() ? "default" : config_.default_device_name;
}

std::string AudioService::effectiveFactoryName(const AudioStream& stream) const {
  if (!stream.driver_factory.empty()) return stream.driver_factory;
  return config_.driver_factory.empty() ? "linux-host" : config_.driver_factory;
}

} // namespace audio_studio::framework::audio
