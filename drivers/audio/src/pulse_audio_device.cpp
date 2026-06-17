#include "pulse_audio_device.hpp"

#include <algorithm>

#include <pulse/error.h>

namespace audio_studio::drivers::audio {
namespace {

class PulseAudioPlaybackDeviceFactory final : public IAudioPlaybackDeviceFactory {
public:
  std::string name() const override { return "pulse"; }
  AudioResult create(const AudioOpenParams& params, std::unique_ptr<IAudioPlaybackDevice>& out) const override {
    out.reset();
    auto device = std::make_unique<PulseAudioPlaybackDevice>();
    auto status = device->open(params);
    if (!status.ok()) return status;
    out = std::move(device);
    return AudioResult::success();
  }
};

class PulseAudioCaptureDeviceFactory final : public IAudioCaptureDeviceFactory {
public:
  std::string name() const override { return "pulse"; }
  AudioResult create(const AudioOpenParams& params, std::unique_ptr<IAudioCaptureDevice>& out) const override {
    out.reset();
    auto device = std::make_unique<PulseAudioCaptureDevice>();
    auto status = device->open(params);
    if (!status.ok()) return status;
    out = std::move(device);
    return AudioResult::success();
  }
};

const bool kPulseAudioPlaybackDeviceRegistered = [] {
  auto status = AudioDeviceRegistry::instance().registerPlaybackFactory(std::make_unique<PulseAudioPlaybackDeviceFactory>());
  (void)status;
  return true;
}();

const bool kPulseAudioCaptureDeviceRegistered = [] {
  auto status = AudioDeviceRegistry::instance().registerCaptureFactory(std::make_unique<PulseAudioCaptureDeviceFactory>());
  (void)status;
  return true;
}();

AudioResult validateParams(const AudioStreamParams& params) {
  if (params.sample_rate == 0) return AudioResult::invalidArgument("audio sample rate is zero");
  if (params.channels == 0) return AudioResult::invalidArgument("audio channels is zero");
  if (params.bytes_per_sample == 0) return AudioResult::invalidArgument("audio bytes per sample is zero");
  return AudioResult::success();
}

AudioResult pulseError(const std::string& operation, int error) {
  return AudioResult::unavailable(operation + " failed: " + pa_strerror(error));
}

AudioResult pulseSampleFormat(uint16_t bytes_per_sample, pa_sample_format_t& format) {
  switch (bytes_per_sample) {
    case 1:
      format = PA_SAMPLE_U8;
      return AudioResult::success();
    case 2:
      format = PA_SAMPLE_S16LE;
      return AudioResult::success();
    case 3:
      format = PA_SAMPLE_S24LE;
      return AudioResult::success();
    case 4:
      format = PA_SAMPLE_S32LE;
      return AudioResult::success();
    default:
      return AudioResult::invalidArgument("unsupported audio bytes per sample: " + std::to_string(bytes_per_sample));
  }
}

const char* pulseDeviceName(const std::string& device_name) {
  if (device_name.empty() || device_name == "default") return nullptr;
  return device_name.c_str();
}

AudioResult makeSampleSpec(const AudioStreamParams& params, pa_sample_spec& spec, size_t& frame_bytes) {
  auto status = validateParams(params);
  if (!status.ok()) return status;
  pa_sample_format_t format = PA_SAMPLE_INVALID;
  status = pulseSampleFormat(params.bytes_per_sample, format);
  if (!status.ok()) return status;
  spec.format = format;
  spec.rate = params.sample_rate;
  spec.channels = static_cast<uint8_t>(params.channels);
  if (!pa_sample_spec_valid(&spec)) return AudioResult::invalidArgument("invalid PulseAudio sample spec");
  frame_bytes = static_cast<size_t>(params.channels) * params.bytes_per_sample;
  return AudioResult::success();
}

} // namespace

PulseAudioPlaybackDevice::~PulseAudioPlaybackDevice() {
  close();
}

AudioResult PulseAudioPlaybackDevice::open(const AudioOpenParams& params) {
  close();
  device_name_ = params.device_name;
  blocking_write_ = params.blocking_write;
  prepared_ = false;
  running_ = false;
  return AudioResult::success();
}

AudioResult PulseAudioPlaybackDevice::prepare(const AudioStreamParams& params) {
  close();
  params_ = params;
  pa_sample_spec spec {};
  auto status = makeSampleSpec(params_, spec, frame_bytes_);
  if (!status.ok()) return status;

  int error = 0;
  stream_ = pa_simple_new(nullptr,
                          "Audio Studio",
                          PA_STREAM_PLAYBACK,
                          pulseDeviceName(device_name_),
                          "Audio Studio Playback",
                          &spec,
                          nullptr,
                          nullptr,
                          &error);
  if (stream_ == nullptr) return pulseError("pa_simple_new playback " + device_name_, error);
  prepared_ = true;
  return AudioResult::success();
}

AudioResult PulseAudioPlaybackDevice::start() {
  if (!prepared_ || stream_ == nullptr) return AudioResult::unavailable("PulseAudio playback device is not prepared");
  running_ = true;
  return AudioResult::success();
}

AudioResult PulseAudioPlaybackDevice::writeFrame(const AudioFrame& frame, uint32_t) {
  if (!running_ || stream_ == nullptr) return AudioResult::unavailable("PulseAudio playback device is not running");
  if (frame.empty()) return AudioResult::invalidArgument("audio frame is empty");
  if (frame_bytes_ == 0 || frame.size() % frame_bytes_ != 0) return AudioResult::invalidArgument("audio frame size is not aligned to sample frame");

  int error = 0;
  if (pa_simple_write(stream_, frame.data(), frame.size(), &error) < 0) return pulseError("pa_simple_write", error);
  frames_written_ += frame.size() / frame_bytes_;
  return AudioResult::success();
}

AudioResult PulseAudioPlaybackDevice::drain() {
  if (stream_ == nullptr) return AudioResult::unavailable("PulseAudio playback device is not open");
  int error = 0;
  if (pa_simple_drain(stream_, &error) < 0) return pulseError("pa_simple_drain", error);
  return AudioResult::success();
}

AudioResult PulseAudioPlaybackDevice::stop() {
  if (stream_ == nullptr) return AudioResult::unavailable("PulseAudio playback device is not open");
  int error = 0;
  if (pa_simple_flush(stream_, &error) < 0) return pulseError("pa_simple_flush playback", error);
  running_ = false;
  return AudioResult::success();
}

void PulseAudioPlaybackDevice::close() {
  if (stream_ != nullptr) {
    pa_simple_free(stream_);
    stream_ = nullptr;
  }
  prepared_ = false;
  running_ = false;
  frame_bytes_ = 0;
}

AudioStreamStats PulseAudioPlaybackDevice::getStats() const {
  return {frames_written_, 0, running_};
}

AudioDeviceCaps PulseAudioPlaybackDevice::getCaps() const {
  return {};
}

PulseAudioCaptureDevice::~PulseAudioCaptureDevice() {
  close();
}

AudioResult PulseAudioCaptureDevice::open(const AudioOpenParams& params) {
  close();
  device_name_ = params.device_name;
  prepared_ = false;
  running_ = false;
  return AudioResult::success();
}

AudioResult PulseAudioCaptureDevice::prepare(const AudioStreamParams& params) {
  close();
  params_ = params;
  pa_sample_spec spec {};
  auto status = makeSampleSpec(params_, spec, frame_bytes_);
  if (!status.ok()) return status;

  int error = 0;
  stream_ = pa_simple_new(nullptr,
                          "Audio Studio",
                          PA_STREAM_RECORD,
                          pulseDeviceName(device_name_),
                          "Audio Studio Capture",
                          &spec,
                          nullptr,
                          nullptr,
                          &error);
  if (stream_ == nullptr) return pulseError("pa_simple_new capture " + device_name_, error);
  prepared_ = true;
  return AudioResult::success();
}

AudioResult PulseAudioCaptureDevice::start() {
  if (!prepared_ || stream_ == nullptr) return AudioResult::unavailable("PulseAudio capture device is not prepared");
  running_ = true;
  return AudioResult::success();
}

AudioResult PulseAudioCaptureDevice::readFrame(AudioFrame& frame, uint32_t) {
  if (!running_ || stream_ == nullptr) return AudioResult::unavailable("PulseAudio capture device is not running");
  if (frame_bytes_ == 0) return AudioResult::unavailable("PulseAudio capture device is not configured");
  if (frame.empty()) frame.resize(frame_bytes_ * 256);
  frame.resize(std::max<size_t>(frame_bytes_, (frame.size() / frame_bytes_) * frame_bytes_));

  int error = 0;
  if (pa_simple_read(stream_, frame.data(), frame.size(), &error) < 0) return pulseError("pa_simple_read", error);
  frames_read_ += frame.size() / frame_bytes_;
  return AudioResult::success();
}

AudioResult PulseAudioCaptureDevice::stop() {
  if (stream_ == nullptr) return AudioResult::unavailable("PulseAudio capture device is not open");
  int error = 0;
  if (pa_simple_flush(stream_, &error) < 0) return pulseError("pa_simple_flush capture", error);
  running_ = false;
  return AudioResult::success();
}

void PulseAudioCaptureDevice::close() {
  if (stream_ != nullptr) {
    pa_simple_free(stream_);
    stream_ = nullptr;
  }
  prepared_ = false;
  running_ = false;
  frame_bytes_ = 0;
}

AudioStreamStats PulseAudioCaptureDevice::getStats() const {
  return {0, frames_read_, running_};
}

AudioDeviceCaps PulseAudioCaptureDevice::getCaps() const {
  return {};
}

} // namespace audio_studio::drivers::audio
