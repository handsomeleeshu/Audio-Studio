#include "audio_studio/drivers/audio/audio_device.hpp"

#include <utility>

namespace audio_studio::drivers::audio {

framework::Status AudioDevice::open(AudioDirection direction, AudioFormat format) {
  if (format.sample_rate == 0) return framework::Status::invalidArgument("audio sample rate is zero");
  if (format.channels == 0) return framework::Status::invalidArgument("audio channels is zero");
  if (format.bytes_per_sample == 0) return framework::Status::invalidArgument("audio bytes per sample is zero");
  direction_ = direction;
  format_ = format;
  open_ = true;
  running_ = false;
  return framework::Status::success();
}

framework::Status AudioDevice::start() {
  if (!open_) return framework::Status::unavailable("audio device is not open");
  running_ = true;
  return framework::Status::success();
}

framework::Status AudioDevice::writeFrame(const std::vector<uint8_t>& frame) {
  if (!running_) return framework::Status::unavailable("audio device is not running");
  if (direction_ != AudioDirection::kPlayback) return framework::Status::invalidArgument("audio device is not playback");
  if (frame.empty()) return framework::Status::invalidArgument("audio frame is empty");
  ++frames_written_;
  return framework::Status::success();
}

framework::Status AudioDevice::injectCaptureFrame(const std::vector<uint8_t>& frame) {
  if (direction_ != AudioDirection::kCapture) return framework::Status::invalidArgument("audio device is not capture");
  if (frame.empty()) return framework::Status::invalidArgument("audio frame is empty");
  capture_frames_.push_back(frame);
  return framework::Status::success();
}

framework::Status AudioDevice::readFrame(std::vector<uint8_t>& out) {
  if (!running_) return framework::Status::unavailable("audio device is not running");
  if (direction_ != AudioDirection::kCapture) return framework::Status::invalidArgument("audio device is not capture");
  if (capture_frames_.empty()) return framework::Status::unavailable("no capture frame available");
  out = std::move(capture_frames_.front());
  capture_frames_.erase(capture_frames_.begin());
  ++frames_read_;
  return framework::Status::success();
}

framework::Status AudioDevice::stop() {
  if (!open_) return framework::Status::unavailable("audio device is not open");
  running_ = false;
  return framework::Status::success();
}

void AudioDevice::close() {
  open_ = false;
  running_ = false;
  capture_frames_.clear();
}

AudioStats AudioDevice::stats() const {
  return {frames_written_, frames_read_, running_};
}

bool AudioDevice::isOpen() const {
  return open_;
}

} // namespace audio_studio::drivers::audio
