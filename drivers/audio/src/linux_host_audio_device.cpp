#include "linux_host_audio_device.hpp"

#include <algorithm>

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

AudioResult alsaError(const std::string& operation, int error) {
  return AudioResult::unavailable(operation + " failed: " + snd_strerror(error));
}

AudioResult sampleFormat(uint16_t bytes_per_sample, snd_pcm_format_t& format) {
  switch (bytes_per_sample) {
    case 1:
      format = SND_PCM_FORMAT_U8;
      return AudioResult::success();
    case 2:
      format = SND_PCM_FORMAT_S16_LE;
      return AudioResult::success();
    case 3:
      format = SND_PCM_FORMAT_S24_3LE;
      return AudioResult::success();
    case 4:
      format = SND_PCM_FORMAT_S32_LE;
      return AudioResult::success();
    default:
      return AudioResult::invalidArgument("unsupported audio bytes per sample: " + std::to_string(bytes_per_sample));
  }
}

AudioResult configurePcmHandle(snd_pcm_t* pcm, const AudioStreamParams& params, size_t& frame_bytes) {
  snd_pcm_format_t format = SND_PCM_FORMAT_UNKNOWN;
  auto status = sampleFormat(params.bytes_per_sample, format);
  if (!status.ok()) return status;

  snd_pcm_hw_params_t* hw_params = nullptr;
  snd_pcm_hw_params_alloca(&hw_params);

  int rc = snd_pcm_hw_params_any(pcm, hw_params);
  if (rc < 0) return alsaError("snd_pcm_hw_params_any", rc);
  rc = snd_pcm_hw_params_set_access(pcm, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
  if (rc < 0) return alsaError("snd_pcm_hw_params_set_access", rc);
  rc = snd_pcm_hw_params_set_format(pcm, hw_params, format);
  if (rc < 0) return alsaError("snd_pcm_hw_params_set_format", rc);
  rc = snd_pcm_hw_params_set_channels(pcm, hw_params, params.channels);
  if (rc < 0) return alsaError("snd_pcm_hw_params_set_channels", rc);

  unsigned int rate = params.sample_rate;
  rc = snd_pcm_hw_params_set_rate_near(pcm, hw_params, &rate, nullptr);
  if (rc < 0) return alsaError("snd_pcm_hw_params_set_rate_near", rc);

  snd_pcm_uframes_t period_size = 256;
  (void)snd_pcm_hw_params_set_period_size_near(pcm, hw_params, &period_size, nullptr);

  rc = snd_pcm_hw_params(pcm, hw_params);
  if (rc < 0) return alsaError("snd_pcm_hw_params", rc);

  rc = snd_pcm_prepare(pcm);
  if (rc < 0) return alsaError("snd_pcm_prepare", rc);

  frame_bytes = static_cast<size_t>(params.channels) * params.bytes_per_sample;
  return AudioResult::success();
}

} // namespace

LinuxHostAudioPlaybackDevice::~LinuxHostAudioPlaybackDevice() {
  close();
}

AudioResult LinuxHostAudioPlaybackDevice::open(const AudioOpenParams& params) {
  if (params.device_name.empty()) return AudioResult::invalidArgument("audio playback device name is empty");
  device_name_ = params.device_name;
  prepared_ = false;
  running_ = false;
  return AudioResult::success();
}

AudioResult LinuxHostAudioPlaybackDevice::prepare(const AudioStreamParams& params) {
  if (device_name_.empty()) return AudioResult::unavailable("audio playback device is not open");
  auto status = validateParams(params);
  if (!status.ok()) return status;
  if (pcm_ != nullptr) close();

  params_ = params;
  int rc = snd_pcm_open(&pcm_, device_name_.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
  if (rc < 0) {
    pcm_ = nullptr;
    return alsaError("snd_pcm_open playback " + device_name_, rc);
  }

  status = configurePcm();
  if (!status.ok()) {
    close();
    return status;
  }
  prepared_ = true;
  return AudioResult::success();
}

AudioResult LinuxHostAudioPlaybackDevice::start() {
  if (!prepared_) return AudioResult::unavailable("audio playback device is not prepared");
  running_ = true;
  return AudioResult::success();
}

AudioResult LinuxHostAudioPlaybackDevice::writeFrame(const AudioFrame& frame, uint32_t timeout_ms) {
  if (!running_) return AudioResult::unavailable("audio playback device is not running");
  if (frame.empty()) return AudioResult::invalidArgument("audio frame is empty");
  if (frame_bytes_ == 0 || frame.size() % frame_bytes_ != 0) return AudioResult::invalidArgument("audio frame size is not aligned to sample frame");

  int rc = snd_pcm_wait(pcm_, static_cast<int>(timeout_ms));
  if (rc == 0) return AudioResult::unavailable("audio playback wait timed out");
  if (rc < 0) return recover(rc);

  const auto total_frames = static_cast<snd_pcm_uframes_t>(frame.size() / frame_bytes_);
  snd_pcm_uframes_t written = 0;
  while (written < total_frames) {
    rc = snd_pcm_writei(pcm_, frame.data() + written * frame_bytes_, total_frames - written);
    if (rc < 0) {
      auto status = recover(rc);
      if (!status.ok()) return status;
      continue;
    }
    written += static_cast<snd_pcm_uframes_t>(rc);
  }

  frames_written_ += static_cast<size_t>(total_frames);
  return AudioResult::success();
}

AudioResult LinuxHostAudioPlaybackDevice::drain() {
  if (pcm_ == nullptr) return AudioResult::unavailable("audio playback device is not open");
  const int rc = snd_pcm_drain(pcm_);
  if (rc < 0) return recover(rc);
  return AudioResult::success();
}

AudioResult LinuxHostAudioPlaybackDevice::stop() {
  if (pcm_ == nullptr) return AudioResult::unavailable("audio playback device is not open");
  const int rc = snd_pcm_drop(pcm_);
  if (rc < 0) return recover(rc);
  (void)snd_pcm_prepare(pcm_);
  running_ = false;
  return AudioResult::success();
}

void LinuxHostAudioPlaybackDevice::close() {
  if (pcm_ != nullptr) {
    snd_pcm_close(pcm_);
    pcm_ = nullptr;
  }
  prepared_ = false;
  running_ = false;
  frame_bytes_ = 0;
}

AudioStreamStats LinuxHostAudioPlaybackDevice::getStats() const {
  return {frames_written_, 0, running_};
}

AudioDeviceCaps LinuxHostAudioPlaybackDevice::getCaps() const {
  return {};
}

AudioResult LinuxHostAudioPlaybackDevice::configurePcm() {
  return configurePcmHandle(pcm_, params_, frame_bytes_);
}

AudioResult LinuxHostAudioPlaybackDevice::recover(int error) {
  const int rc = snd_pcm_recover(pcm_, error, 1);
  if (rc < 0) return alsaError("snd_pcm_recover playback", rc);
  return AudioResult::success();
}

LinuxHostAudioCaptureDevice::~LinuxHostAudioCaptureDevice() {
  close();
}

AudioResult LinuxHostAudioCaptureDevice::open(const AudioOpenParams& params) {
  if (params.device_name.empty()) return AudioResult::invalidArgument("audio capture device name is empty");
  device_name_ = params.device_name;
  prepared_ = false;
  running_ = false;
  return AudioResult::success();
}

AudioResult LinuxHostAudioCaptureDevice::prepare(const AudioStreamParams& params) {
  if (device_name_.empty()) return AudioResult::unavailable("audio capture device is not open");
  auto status = validateParams(params);
  if (!status.ok()) return status;
  if (pcm_ != nullptr) close();

  params_ = params;
  int rc = snd_pcm_open(&pcm_, device_name_.c_str(), SND_PCM_STREAM_CAPTURE, 0);
  if (rc < 0) {
    pcm_ = nullptr;
    return alsaError("snd_pcm_open capture " + device_name_, rc);
  }

  status = configurePcm();
  if (!status.ok()) {
    close();
    return status;
  }
  prepared_ = true;
  return AudioResult::success();
}

AudioResult LinuxHostAudioCaptureDevice::start() {
  if (!prepared_) return AudioResult::unavailable("audio capture device is not prepared");
  const int rc = snd_pcm_prepare(pcm_);
  if (rc < 0) return alsaError("snd_pcm_prepare capture", rc);
  const int start_rc = snd_pcm_start(pcm_);
  if (start_rc < 0) {
    auto status = recover(start_rc);
    if (!status.ok()) return status;
  }
  running_ = true;
  return AudioResult::success();
}

AudioResult LinuxHostAudioCaptureDevice::readFrame(AudioFrame& frame, uint32_t timeout_ms) {
  if (!running_) return AudioResult::unavailable("audio capture device is not running");
  if (frame_bytes_ == 0) return AudioResult::unavailable("audio capture device is not configured");

  int rc = snd_pcm_wait(pcm_, static_cast<int>(timeout_ms));
  if (rc == 0) return AudioResult::unavailable("audio capture wait timed out");
  if (rc < 0) return recover(rc);

  size_t requested_frames = frame.size() >= frame_bytes_ ? frame.size() / frame_bytes_ : 256;
  if (requested_frames == 0) requested_frames = 1;
  snd_pcm_sframes_t available = snd_pcm_avail_update(pcm_);
  if (available < 0) {
    auto status = recover(static_cast<int>(available));
    if (!status.ok()) return status;
    available = snd_pcm_avail_update(pcm_);
  }
  if (available == 0) return AudioResult::unavailable("audio capture has no frames available after wait");

  const auto frames_to_read = static_cast<snd_pcm_uframes_t>(
    std::min<size_t>(requested_frames, available > 0 ? static_cast<size_t>(available) : requested_frames));
  frame.assign(static_cast<size_t>(frames_to_read) * frame_bytes_, 0);
  rc = snd_pcm_readi(pcm_, frame.data(), frames_to_read);
  if (rc < 0) {
    auto status = recover(rc);
    if (!status.ok()) return status;
    rc = snd_pcm_readi(pcm_, frame.data(), static_cast<snd_pcm_uframes_t>(requested_frames));
    if (rc < 0) return alsaError("snd_pcm_readi", rc);
  }
  if (rc == 0) return AudioResult::unavailable("audio capture read returned no frames");
  frame.resize(static_cast<size_t>(rc) * frame_bytes_);
  frames_read_ += static_cast<size_t>(rc);
  return AudioResult::success();
}

AudioResult LinuxHostAudioCaptureDevice::stop() {
  if (pcm_ == nullptr) return AudioResult::unavailable("audio capture device is not open");
  const int rc = snd_pcm_drop(pcm_);
  if (rc < 0) return recover(rc);
  (void)snd_pcm_prepare(pcm_);
  running_ = false;
  return AudioResult::success();
}

void LinuxHostAudioCaptureDevice::close() {
  if (pcm_ != nullptr) {
    snd_pcm_close(pcm_);
    pcm_ = nullptr;
  }
  prepared_ = false;
  running_ = false;
  frame_bytes_ = 0;
}

AudioStreamStats LinuxHostAudioCaptureDevice::getStats() const {
  return {0, frames_read_, running_};
}

AudioDeviceCaps LinuxHostAudioCaptureDevice::getCaps() const {
  return {};
}

AudioResult LinuxHostAudioCaptureDevice::configurePcm() {
  return configurePcmHandle(pcm_, params_, frame_bytes_);
}

AudioResult LinuxHostAudioCaptureDevice::recover(int error) {
  const int rc = snd_pcm_recover(pcm_, error, 1);
  if (rc < 0) return alsaError("snd_pcm_recover capture", rc);
  return AudioResult::success();
}

} // namespace audio_studio::drivers::audio
