#include "linux_host_audio_device.hpp"

#include <utility>

namespace audio_studio::drivers::audio {

namespace {

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

const bool kLinuxHostAudioPlaybackDeviceRegistered = [] {
  auto status = AudioDeviceRegistry::instance().registerPlaybackFactory(std::make_unique<LinuxHostAudioPlaybackDeviceFactory>());
  (void)status;
  return true;
}();

const bool kLinuxHostAudioCaptureDeviceRegistered = [] {
  auto status = AudioDeviceRegistry::instance().registerCaptureFactory(std::make_unique<LinuxHostAudioCaptureDeviceFactory>());
  (void)status;
  return true;
}();

AudioResult validateParams(const AudioStreamParams& params) {
  if (params.sample_rate == 0) return AudioResult::invalidArgument("audio sample rate is zero");
  if (params.channels == 0) return AudioResult::invalidArgument("audio channels is zero");
  if (params.bytes_per_sample == 0) return AudioResult::invalidArgument("audio bytes per sample is zero");
  return AudioResult::success();
}

} // namespace

AudioResult LinuxHostAudioPlaybackDevice::open(const AudioOpenParams& params) {
  if (params.device_name.empty()) return AudioResult::invalidArgument("audio playback device name is empty");
  device_name_ = params.device_name;
  open_ = true;
  prepared_ = false;
  running_ = false;
  return AudioResult::success();
}

AudioResult LinuxHostAudioPlaybackDevice::prepare(const AudioStreamParams& params) {
  if (!open_) return AudioResult::unavailable("audio playback device is not open");
  auto status = validateParams(params);
  if (!status.ok()) return status;
  params_ = params;
  prepared_ = true;
  return AudioResult::success();
}

AudioResult LinuxHostAudioPlaybackDevice::start() {
  if (!prepared_) return AudioResult::unavailable("audio playback device is not prepared");
  running_ = true;
  return AudioResult::success();
}

AudioResult LinuxHostAudioPlaybackDevice::writeFrame(const AudioFrame& frame, uint32_t /*timeout_ms*/) {
  if (!running_) return AudioResult::unavailable("audio playback device is not running");
  if (frame.empty()) return AudioResult::invalidArgument("audio frame is empty");
  ++frames_written_;
  return AudioResult::success();
}

AudioResult LinuxHostAudioPlaybackDevice::drain() {
  if (!open_) return AudioResult::unavailable("audio playback device is not open");
  return AudioResult::success();
}

AudioResult LinuxHostAudioPlaybackDevice::stop() {
  if (!open_) return AudioResult::unavailable("audio playback device is not open");
  running_ = false;
  return AudioResult::success();
}

void LinuxHostAudioPlaybackDevice::close() {
  open_ = false;
  prepared_ = false;
  running_ = false;
}

AudioStreamStats LinuxHostAudioPlaybackDevice::getStats() const {
  return {frames_written_, 0, running_};
}

AudioDeviceCaps LinuxHostAudioPlaybackDevice::getCaps() const {
  return {};
}

AudioResult LinuxHostAudioCaptureDevice::open(const AudioOpenParams& params) {
  if (params.device_name.empty()) return AudioResult::invalidArgument("audio capture device name is empty");
  device_name_ = params.device_name;
  open_ = true;
  prepared_ = false;
  running_ = false;
  return AudioResult::success();
}

AudioResult LinuxHostAudioCaptureDevice::prepare(const AudioStreamParams& params) {
  if (!open_) return AudioResult::unavailable("audio capture device is not open");
  auto status = validateParams(params);
  if (!status.ok()) return status;
  params_ = params;
  prepared_ = true;
  return AudioResult::success();
}

AudioResult LinuxHostAudioCaptureDevice::start() {
  if (!prepared_) return AudioResult::unavailable("audio capture device is not prepared");
  if (capture_frames_.empty()) {
    capture_frames_.push_back(AudioFrame(params_.channels * params_.bytes_per_sample, 0));
  }
  running_ = true;
  return AudioResult::success();
}

AudioResult LinuxHostAudioCaptureDevice::readFrame(AudioFrame& frame, uint32_t /*timeout_ms*/) {
  if (!running_) return AudioResult::unavailable("audio capture device is not running");
  if (capture_frames_.empty()) return AudioResult::unavailable("no capture frame available");
  frame = std::move(capture_frames_.front());
  capture_frames_.erase(capture_frames_.begin());
  ++frames_read_;
  return AudioResult::success();
}

AudioResult LinuxHostAudioCaptureDevice::stop() {
  if (!open_) return AudioResult::unavailable("audio capture device is not open");
  running_ = false;
  return AudioResult::success();
}

void LinuxHostAudioCaptureDevice::close() {
  open_ = false;
  prepared_ = false;
  running_ = false;
  capture_frames_.clear();
}

AudioStreamStats LinuxHostAudioCaptureDevice::getStats() const {
  return {0, frames_read_, running_};
}

AudioDeviceCaps LinuxHostAudioCaptureDevice::getCaps() const {
  return {};
}

AudioResult LinuxHostAudioCaptureDevice::injectCaptureFrame(AudioFrame frame) {
  if (frame.empty()) return AudioResult::invalidArgument("audio frame is empty");
  capture_frames_.push_back(std::move(frame));
  return AudioResult::success();
}

} // namespace audio_studio::drivers::audio
