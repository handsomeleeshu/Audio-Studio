#include "audio_service.hpp"

#include <algorithm>
#include <utility>

namespace audio_studio::framework::audio {
namespace {

drivers::audio::AudioFrame makeFrame(const std::vector<uint8_t>& data) {
  return drivers::audio::AudioFrame(data.begin(), data.end());
}

framework::Status requirePlaybackDirection(const AudioStream& stream) {
  if (stream.direction != StreamDirection::kPlayback) {
    return framework::Status::invalidArgument("audio operation requires playback stream: " + stream.id);
  }
  return framework::Status::success();
}

framework::Status requireCaptureDirection(const AudioStream& stream) {
  if (stream.direction != StreamDirection::kCapture) {
    return framework::Status::invalidArgument("audio operation requires capture stream: " + stream.id);
  }
  return framework::Status::success();
}

} // namespace

AudioPlaybackSession::AudioPlaybackSession(AudioStream stream, std::unique_ptr<drivers::audio::IAudioPlaybackDevice> device)
  : stream_(std::move(stream)), device_(std::move(device)) {}

AudioPlaybackSession::~AudioPlaybackSession() {
  (void)close();
}

framework::Status AudioPlaybackSession::prepare() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_) return framework::Status::unavailable("audio playback session is closed: " + stream_.id);
  if (stream_.prepared) return framework::Status::success();
  if (device_) {
    auto status = device_->prepare(streamParamsLocked());
    if (!status.ok()) return status;
  }
  stream_.prepared = true;
  return framework::Status::success();
}

framework::Status AudioPlaybackSession::start() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_) return framework::Status::unavailable("audio playback session is closed: " + stream_.id);
  if (!stream_.prepared) {
    if (device_) {
      auto status = device_->prepare(streamParamsLocked());
      if (!status.ok()) return status;
    }
    stream_.prepared = true;
  }
  if (device_) {
    auto status = device_->start();
    if (!status.ok()) return status;
  }
  stream_.running = true;
  return framework::Status::success();
}

framework::Status AudioPlaybackSession::writeFrames(const std::vector<uint8_t>& data,
                                                    uint32_t timeout_ms,
                                                    size_t& accepted_bytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  accepted_bytes = 0;
  if (closed_) return framework::Status::unavailable("audio playback session is closed: " + stream_.id);
  if (!stream_.running) return framework::Status::unavailable("audio playback stream is not running: " + stream_.id);
  if (data.empty()) return framework::Status::success();

  if (device_) {
    auto frame = makeFrame(data);
    auto status = device_->writeFrame(frame, timeout_ms);
    if (!status.ok()) return status;
  }
  accepted_bytes = data.size();
  return framework::Status::success();
}

framework::Status AudioPlaybackSession::drain() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_) return framework::Status::unavailable("audio playback session is closed: " + stream_.id);
  if (device_) return device_->drain();
  return framework::Status::success();
}

framework::Status AudioPlaybackSession::stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_) return framework::Status::success();
  if (device_) {
    auto status = device_->stop();
    if (!status.ok()) return status;
  }
  stream_.running = false;
  return framework::Status::success();
}

framework::Status AudioPlaybackSession::close() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_) return framework::Status::success();
  if (device_) device_->close();
  stream_.running = false;
  stream_.prepared = false;
  closed_ = true;
  return framework::Status::success();
}

framework::Status AudioPlaybackSession::snapshot(AudioStream& out) const {
  std::lock_guard<std::mutex> lock(mutex_);
  out = stream_;
  return framework::Status::success();
}

framework::Status AudioPlaybackSession::getStats(AudioIoStats& out) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (device_) {
    auto stats = device_->getStats();
    out = {stats.frames_written, stats.frames_read, stats.running};
    return framework::Status::success();
  }
  out = {};
  out.running = stream_.running;
  return framework::Status::success();
}

const std::string& AudioPlaybackSession::id() const {
  return stream_.id;
}

drivers::audio::AudioStreamParams AudioPlaybackSession::streamParamsLocked() const {
  drivers::audio::AudioStreamParams params;
  params.sample_rate = static_cast<uint32_t>(stream_.sample_rate);
  params.channels = static_cast<uint16_t>(stream_.channels);
  params.bytes_per_sample = static_cast<uint16_t>(stream_.bytes_per_sample);
  return params;
}

AudioCaptureSession::AudioCaptureSession(AudioStream stream, std::unique_ptr<drivers::audio::IAudioCaptureDevice> device)
  : stream_(std::move(stream)), device_(std::move(device)) {}

AudioCaptureSession::~AudioCaptureSession() {
  (void)close();
}

framework::Status AudioCaptureSession::prepare() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_) return framework::Status::unavailable("audio capture session is closed: " + stream_.id);
  if (stream_.prepared) return framework::Status::success();
  if (device_) {
    auto status = device_->prepare(streamParamsLocked());
    if (!status.ok()) return status;
  }
  stream_.prepared = true;
  return framework::Status::success();
}

framework::Status AudioCaptureSession::start() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_) return framework::Status::unavailable("audio capture session is closed: " + stream_.id);
  if (!stream_.prepared) {
    if (device_) {
      auto status = device_->prepare(streamParamsLocked());
      if (!status.ok()) return status;
    }
    stream_.prepared = true;
  }
  if (device_) {
    auto status = device_->start();
    if (!status.ok()) return status;
  }
  stream_.running = true;
  return framework::Status::success();
}

framework::Status AudioCaptureSession::readFrames(size_t max_bytes,
                                                  uint32_t timeout_ms,
                                                  std::vector<uint8_t>& data) {
  std::lock_guard<std::mutex> lock(mutex_);
  data.clear();
  if (closed_) return framework::Status::unavailable("audio capture session is closed: " + stream_.id);
  if (!stream_.running) return framework::Status::unavailable("audio capture stream is not running: " + stream_.id);
  if (max_bytes == 0) return framework::Status::invalidArgument("audio read max_bytes is zero");

  if (!device_) {
    data.assign(max_bytes, 0);
    return framework::Status::success();
  }

  const size_t frame_bytes = static_cast<size_t>(stream_.channels) * static_cast<size_t>(stream_.bytes_per_sample);
  if (frame_bytes == 0) return framework::Status::invalidArgument("audio capture frame size is zero");
  const size_t aligned_max = std::max(frame_bytes, (max_bytes / frame_bytes) * frame_bytes);
  drivers::audio::AudioFrame frame(aligned_max, 0);
  auto status = device_->readFrame(frame, timeout_ms);
  if (!status.ok()) return status;
  if (frame.size() > max_bytes) frame.resize((max_bytes / frame_bytes) * frame_bytes);
  data.assign(frame.begin(), frame.end());
  return framework::Status::success();
}

framework::Status AudioCaptureSession::stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_) return framework::Status::success();
  if (device_) {
    auto status = device_->stop();
    if (!status.ok()) return status;
  }
  stream_.running = false;
  return framework::Status::success();
}

framework::Status AudioCaptureSession::close() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_) return framework::Status::success();
  if (device_) device_->close();
  stream_.running = false;
  stream_.prepared = false;
  closed_ = true;
  return framework::Status::success();
}

framework::Status AudioCaptureSession::snapshot(AudioStream& out) const {
  std::lock_guard<std::mutex> lock(mutex_);
  out = stream_;
  return framework::Status::success();
}

framework::Status AudioCaptureSession::getStats(AudioIoStats& out) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (device_) {
    auto stats = device_->getStats();
    out = {stats.frames_written, stats.frames_read, stats.running};
    return framework::Status::success();
  }
  out = {};
  out.running = stream_.running;
  return framework::Status::success();
}

const std::string& AudioCaptureSession::id() const {
  return stream_.id;
}

drivers::audio::AudioStreamParams AudioCaptureSession::streamParamsLocked() const {
  drivers::audio::AudioStreamParams params;
  params.sample_rate = static_cast<uint32_t>(stream_.sample_rate);
  params.channels = static_cast<uint16_t>(stream_.channels);
  params.bytes_per_sample = static_cast<uint16_t>(stream_.bytes_per_sample);
  return params;
}

AudioService::AudioService() = default;
AudioService::~AudioService() = default;

void AudioService::configureDeviceRegistry(drivers::audio::AudioDeviceRegistry* registry, AudioServiceConfig config) {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  registry_ = registry;
  config_ = std::move(config);
}

framework::Status AudioService::createPlaybackSession(AudioStream stream, std::shared_ptr<AudioPlaybackSession>& out) {
  out.reset();
  auto status = validateStream(stream);
  if (!status.ok()) return status;
  status = requirePlaybackDirection(stream);
  if (!status.ok()) return status;

  stream.driver_factory = effectiveFactoryName(stream);
  stream.device_name = effectiveDeviceName(stream);

  {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    if (hasSessionLocked(stream.id)) {
      return framework::Status::invalidArgument("audio stream already exists: " + stream.id);
    }
  }

  std::unique_ptr<drivers::audio::IAudioPlaybackDevice> device;
  if (registry_ != nullptr) {
    const drivers::audio::AudioOpenParams open_params {stream.device_name, stream.blocking_write, config_.options};
    status = registry_->createPlayback(stream.driver_factory, open_params, device);
    if (!status.ok()) {
      return framework::Status::unavailable("failed to create playback audio driver: " + stream.driver_factory +
                                            " device=" + stream.device_name + ": " + status.message());
    }
  }

  auto session = std::make_shared<AudioPlaybackSession>(std::move(stream), std::move(device));
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  if (hasSessionLocked(session->id())) {
    return framework::Status::invalidArgument("audio stream already exists: " + session->id());
  }
  playback_sessions_.emplace(session->id(), session);
  out = std::move(session);
  return framework::Status::success();
}

framework::Status AudioService::createCaptureSession(AudioStream stream, std::shared_ptr<AudioCaptureSession>& out) {
  out.reset();
  auto status = validateStream(stream);
  if (!status.ok()) return status;
  status = requireCaptureDirection(stream);
  if (!status.ok()) return status;

  stream.driver_factory = effectiveFactoryName(stream);
  stream.device_name = effectiveDeviceName(stream);

  {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    if (hasSessionLocked(stream.id)) {
      return framework::Status::invalidArgument("audio stream already exists: " + stream.id);
    }
  }

  std::unique_ptr<drivers::audio::IAudioCaptureDevice> device;
  if (registry_ != nullptr) {
    const drivers::audio::AudioOpenParams open_params {stream.device_name, stream.blocking_write, config_.options};
    status = registry_->createCapture(stream.driver_factory, open_params, device);
    if (!status.ok()) {
      return framework::Status::unavailable("failed to create capture audio driver: " + stream.driver_factory +
                                            " device=" + stream.device_name + ": " + status.message());
    }
  }

  auto session = std::make_shared<AudioCaptureSession>(std::move(stream), std::move(device));
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  if (hasSessionLocked(session->id())) {
    return framework::Status::invalidArgument("audio stream already exists: " + session->id());
  }
  capture_sessions_.emplace(session->id(), session);
  out = std::move(session);
  return framework::Status::success();
}

framework::Status AudioService::create(AudioStream stream) {
  if (stream.direction == StreamDirection::kPlayback) {
    std::shared_ptr<AudioPlaybackSession> session;
    return createPlaybackSession(std::move(stream), session);
  }

  std::shared_ptr<AudioCaptureSession> session;
  return createCaptureSession(std::move(stream), session);
}

framework::Status AudioService::prepare(const std::string& id) {
  if (auto playback = playbackSession(id)) return playback->prepare();
  if (auto capture = captureSession(id)) return capture->prepare();
  return framework::Status::unavailable("audio stream not found: " + id);
}

framework::Status AudioService::start(const std::string& id) {
  if (auto playback = playbackSession(id)) return playback->start();
  if (auto capture = captureSession(id)) return capture->start();
  return framework::Status::unavailable("audio stream not found: " + id);
}

framework::Status AudioService::drain(const std::string& id) {
  auto playback = playbackSession(id);
  if (!playback) return framework::Status::unavailable("audio playback stream not found: " + id);
  return playback->drain();
}

framework::Status AudioService::stop(const std::string& id) {
  if (auto playback = playbackSession(id)) return playback->stop();
  if (auto capture = captureSession(id)) return capture->stop();
  return framework::Status::unavailable("audio stream not found: " + id);
}

framework::Status AudioService::remove(const std::string& id) {
  std::shared_ptr<AudioPlaybackSession> playback;
  std::shared_ptr<AudioCaptureSession> capture;
  {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto playback_it = playback_sessions_.find(id);
    if (playback_it != playback_sessions_.end()) {
      playback = playback_it->second;
      playback_sessions_.erase(playback_it);
    } else {
      auto capture_it = capture_sessions_.find(id);
      if (capture_it != capture_sessions_.end()) {
        capture = capture_it->second;
        capture_sessions_.erase(capture_it);
      }
    }
  }

  if (playback) return playback->close();
  if (capture) return capture->close();
  return framework::Status::unavailable("audio stream not found: " + id);
}

framework::Status AudioService::get(const std::string& id, AudioStream& out) const {
  if (auto playback = playbackSession(id)) return playback->snapshot(out);
  if (auto capture = captureSession(id)) return capture->snapshot(out);
  return framework::Status::unavailable("audio stream not found: " + id);
}

framework::Status AudioService::writeFrames(const std::string& id,
                                            const std::vector<uint8_t>& data,
                                            uint32_t timeout_ms,
                                            size_t& accepted_bytes) {
  auto playback = playbackSession(id);
  if (!playback) return framework::Status::unavailable("audio playback stream not found: " + id);
  return playback->writeFrames(data, timeout_ms, accepted_bytes);
}

framework::Status AudioService::readFrames(const std::string& id,
                                           size_t max_bytes,
                                           uint32_t timeout_ms,
                                           std::vector<uint8_t>& data) {
  auto capture = captureSession(id);
  if (!capture) return framework::Status::unavailable("audio capture stream not found: " + id);
  return capture->readFrames(max_bytes, timeout_ms, data);
}

framework::Status AudioService::getStats(const std::string& id, AudioIoStats& out) const {
  if (auto playback = playbackSession(id)) return playback->getStats(out);
  if (auto capture = captureSession(id)) return capture->getStats(out);
  return framework::Status::unavailable("audio stream not found: " + id);
}

std::vector<AudioStream> AudioService::list() const {
  std::vector<std::shared_ptr<AudioPlaybackSession>> playback;
  std::vector<std::shared_ptr<AudioCaptureSession>> capture;
  {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    playback.reserve(playback_sessions_.size());
    capture.reserve(capture_sessions_.size());
    for (const auto& item : playback_sessions_) playback.push_back(item.second);
    for (const auto& item : capture_sessions_) capture.push_back(item.second);
  }

  std::vector<AudioStream> out;
  out.reserve(playback.size() + capture.size());
  for (const auto& session : playback) {
    AudioStream stream;
    if (session->snapshot(stream).ok()) out.push_back(std::move(stream));
  }
  for (const auto& session : capture) {
    AudioStream stream;
    if (session->snapshot(stream).ok()) out.push_back(std::move(stream));
  }
  return out;
}

std::shared_ptr<AudioPlaybackSession> AudioService::playbackSession(const std::string& id) const {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  const auto it = playback_sessions_.find(id);
  return it == playback_sessions_.end() ? nullptr : it->second;
}

std::shared_ptr<AudioCaptureSession> AudioService::captureSession(const std::string& id) const {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  const auto it = capture_sessions_.find(id);
  return it == capture_sessions_.end() ? nullptr : it->second;
}

std::string AudioService::effectiveDeviceName(const AudioStream& stream) const {
  if (!stream.device_name.empty()) return stream.device_name;
  return config_.default_device_name.empty() ? "default" : config_.default_device_name;
}

std::string AudioService::effectiveFactoryName(const AudioStream& stream) const {
  if (!stream.driver_factory.empty()) return stream.driver_factory;
  return config_.driver_factory.empty() ? defaultAudioDriverFactory() : config_.driver_factory;
}

framework::Status AudioService::validateStream(const AudioStream& stream) const {
  if (stream.id.empty()) return framework::Status::invalidArgument("audio stream id is empty");
  if (stream.sample_rate <= 0) return framework::Status::invalidArgument("audio sample rate must be positive");
  if (stream.channels <= 0) return framework::Status::invalidArgument("audio channel count must be positive");
  if (stream.bytes_per_sample <= 0) return framework::Status::invalidArgument("audio bytes per sample must be positive");
  return framework::Status::success();
}

bool AudioService::hasSessionLocked(const std::string& id) const {
  return playback_sessions_.find(id) != playback_sessions_.end() || capture_sessions_.find(id) != capture_sessions_.end();
}

} // namespace audio_studio::framework::audio
