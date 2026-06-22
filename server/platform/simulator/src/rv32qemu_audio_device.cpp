#include "audio_device.hpp"
#include "transport_manager.hpp"
#include "ac_transport.h"
#include "ac_transport_channel.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace audio_studio::platform::simulator {
namespace {

constexpr uint32_t kAudioControlTimeoutMs = 1000;
constexpr uint32_t kAudioDataTimeoutMs = 5000;

void writeLe16(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
}

void writeLe32(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
}

uint16_t readLe16(const std::vector<uint8_t>& in, size_t offset) {
  return static_cast<uint16_t>(in[offset]) |
         static_cast<uint16_t>(static_cast<uint16_t>(in[offset + 1u]) << 8u);
}

uint32_t readLe32(const std::vector<uint8_t>& in, size_t offset) {
  return static_cast<uint32_t>(in[offset]) |
         (static_cast<uint32_t>(in[offset + 1u]) << 8u) |
         (static_cast<uint32_t>(in[offset + 2u]) << 16u) |
         (static_cast<uint32_t>(in[offset + 3u]) << 24u);
}

std::mutex& audioControlChannelMutex() {
  static std::mutex mutex;
  return mutex;
}

size_t& audioControlChannelRefs() {
  static size_t refs = 0;
  return refs;
}

framework::Status acquireAudioControlChannel(framework::transport::TransportManager& manager) {
  std::lock_guard<std::mutex> lock(audioControlChannelMutex());
  auto& refs = audioControlChannelRefs();
  if (refs == 0) {
    auto status = manager.openChannel(AC_TRANSPORT_CHANNEL_AUDIO_CONTROL, "audio-control");
    if (!status.ok()) return status;
  }
  ++refs;
  return framework::Status::success();
}

void releaseAudioControlChannel(framework::transport::TransportManager& manager) {
  std::lock_guard<std::mutex> lock(audioControlChannelMutex());
  auto& refs = audioControlChannelRefs();
  if (refs == 0) return;
  --refs;
  if (refs == 0) (void)manager.closeChannel(AC_TRANSPORT_CHANNEL_AUDIO_CONTROL);
}

drivers::audio::AudioResult transportPayloadStatus(
    const framework::transport::TransportFrame& response,
    const std::string& fallback) {
  if ((response.flags & framework::transport::kTransportFrameError) == 0) {
    return drivers::audio::AudioResult::success();
  }
  const std::string message(response.payload.begin(), response.payload.end());
  return drivers::audio::AudioResult::unavailable(message.empty() ? fallback : message);
}

drivers::audio::AudioResult validateStreamParams(
    const drivers::audio::AudioStreamParams& params) {
  if (params.sample_rate == 0) return drivers::audio::AudioResult::invalidArgument("audio sample rate is zero");
  if (params.channels == 0) return drivers::audio::AudioResult::invalidArgument("audio channel count is zero");
  if (params.bytes_per_sample != 2 && params.bytes_per_sample != 4) {
    return drivers::audio::AudioResult::invalidArgument(
        "rv32qemu simulator audio supports 16-bit and 32-bit PCM streams");
  }
  return drivers::audio::AudioResult::success();
}

class Rv32QemuAudioSession {
public:
  explicit Rv32QemuAudioSession(uint32_t direction) : direction_(direction) {}

  drivers::audio::AudioResult open(const drivers::audio::AudioOpenParams& params) {
    if (params.device_name.empty()) return drivers::audio::AudioResult::invalidArgument("rv32qemu audio stream name is empty");
    close();
    stream_name_ = params.device_name;

    manager_ = &framework::transport::TransportManager::instance();
    if (!manager_->isDataLinkConfigured()) {
      manager_ = nullptr;
      return drivers::audio::AudioResult::unavailable("rv32qemu transport data-link is not configured");
    }
    auto status = acquireAudioControlChannel(*manager_);
    if (!status.ok()) {
      manager_ = nullptr;
      return status;
    }
    control_channel_acquired_ = true;

    std::vector<uint8_t> payload;
    writeLe32(payload, direction_);
    payload.insert(payload.end(), stream_name_.begin(), stream_name_.end());
    payload.push_back(0);
    framework::transport::TransportFrame response;
    status = manager_->sendSync(AC_TRANSPORT_CHANNEL_AUDIO_CONTROL, AC_TRANSPORT_AUDIO_OPEN,
                                payload, response, kAudioControlTimeoutMs);
    if (!status.ok()) {
      releaseAudioControlChannel(*manager_);
      control_channel_acquired_ = false;
      manager_ = nullptr;
      return status;
    }
    status = transportPayloadStatus(response, "audio open failed");
    if (!status.ok()) {
      releaseAudioControlChannel(*manager_);
      control_channel_acquired_ = false;
      manager_ = nullptr;
      return status;
    }
    if (response.payload.size() < 4u) {
      releaseAudioControlChannel(*manager_);
      control_channel_acquired_ = false;
      manager_ = nullptr;
      return drivers::audio::AudioResult::unavailable("audio open response is too small");
    }
    stream_id_ = readLe32(response.payload, 0);
    open_ = true;
    return drivers::audio::AudioResult::success();
  }

  drivers::audio::AudioResult prepare(const drivers::audio::AudioStreamParams& params) {
    if (!open_ || !manager_) return drivers::audio::AudioResult::unavailable("rv32qemu audio device is not open");
    auto status = validateStreamParams(params);
    if (!status.ok()) return status;
    params_ = params;
    frame_bytes_ = static_cast<size_t>(params.channels) * static_cast<size_t>(params.bytes_per_sample);

    std::vector<uint8_t> payload;
    writeLe32(payload, stream_id_);
    writeLe32(payload, params.sample_rate);
    writeLe16(payload, params.channels);
    writeLe16(payload, params.bytes_per_sample);
    framework::transport::TransportFrame response;
    status = manager_->sendSync(AC_TRANSPORT_CHANNEL_AUDIO_CONTROL, AC_TRANSPORT_AUDIO_CONFIG,
                                payload, response, kAudioControlTimeoutMs);
    if (!status.ok()) return status;
    status = transportPayloadStatus(response, "audio config failed");
    if (!status.ok()) return status;
    prepared_ = true;
    return drivers::audio::AudioResult::success();
  }

  drivers::audio::AudioResult start() {
    if (!prepared_ || !manager_) return drivers::audio::AudioResult::unavailable("rv32qemu audio device is not prepared");
    if (running_) return drivers::audio::AudioResult::success();
    std::vector<uint8_t> payload;
    writeLe32(payload, stream_id_);
    framework::transport::TransportFrame response;
    auto status = manager_->sendSync(AC_TRANSPORT_CHANNEL_AUDIO_CONTROL, AC_TRANSPORT_AUDIO_START,
                                    payload, response, kAudioControlTimeoutMs);
    if (!status.ok()) return status;
    status = transportPayloadStatus(response, "audio start failed");
    if (!status.ok()) return status;
    if (response.payload.size() < 6u) {
      return drivers::audio::AudioResult::unavailable("audio start response is too small");
    }
    data_channel_id_ = readLe16(response.payload, 4u);
    status = manager_->openChannel(data_channel_id_, "audio-data");
    if (!status.ok()) {
      std::vector<uint8_t> stop_payload;
      writeLe32(stop_payload, stream_id_);
      framework::transport::TransportFrame stop_response;
      (void)manager_->sendSync(AC_TRANSPORT_CHANNEL_AUDIO_CONTROL, AC_TRANSPORT_AUDIO_STOP,
                               stop_payload, stop_response, kAudioControlTimeoutMs);
      data_channel_id_ = 0;
      return status;
    }
    running_ = true;
    return drivers::audio::AudioResult::success();
  }

  drivers::audio::AudioResult writeFrame(const drivers::audio::AudioFrame& frame,
                                         uint32_t timeout_ms) {
    if (!running_ || !manager_) return drivers::audio::AudioResult::unavailable("rv32qemu playback stream is not running");
    if (direction_ != AC_TRANSPORT_AUDIO_DIRECTION_PLAYBACK) return drivers::audio::AudioResult::invalidArgument("rv32qemu stream is not playback");
    if (frame.empty()) return drivers::audio::AudioResult::invalidArgument("audio frame is empty");
    if (frame_bytes_ == 0 || frame.size() % frame_bytes_ != 0) {
      return drivers::audio::AudioResult::invalidArgument("audio frame size is not aligned to sample frame");
    }
    const size_t max_payload = (AC_TRANSPORT_MAX_PAYLOAD / frame_bytes_) * frame_bytes_;
    if (max_payload == 0) return drivers::audio::AudioResult::invalidArgument("audio frame is larger than transport payload");

    size_t offset = 0;
    while (offset < frame.size()) {
      const size_t chunk_size = std::min(max_payload, frame.size() - offset);
      std::vector<uint8_t> payload(frame.begin() + static_cast<std::ptrdiff_t>(offset),
                                   frame.begin() + static_cast<std::ptrdiff_t>(offset + chunk_size));
      framework::transport::TransportFrame response;
      auto status = manager_->sendSync(data_channel_id_, AC_TRANSPORT_AUDIO_WRITE, payload, response,
                                      timeout_ms == 0 ? kAudioDataTimeoutMs : timeout_ms);
      if (!status.ok()) return status;
      status = transportPayloadStatus(response, "audio write failed");
      if (!status.ok()) return status;
      offset += chunk_size;
    }
    frames_written_ += frame.size() / frame_bytes_;
    return drivers::audio::AudioResult::success();
  }

  drivers::audio::AudioResult readFrame(drivers::audio::AudioFrame& frame,
                                        uint32_t timeout_ms) {
    if (!running_ || !manager_) return drivers::audio::AudioResult::unavailable("rv32qemu capture stream is not running");
    if (direction_ != AC_TRANSPORT_AUDIO_DIRECTION_CAPTURE) return drivers::audio::AudioResult::invalidArgument("rv32qemu stream is not capture");
    if (frame_bytes_ == 0) return drivers::audio::AudioResult::unavailable("rv32qemu capture stream is not configured");
    size_t max_bytes = frame.empty() ? AC_TRANSPORT_MAX_PAYLOAD : frame.size();
    max_bytes = std::max(frame_bytes_, (max_bytes / frame_bytes_) * frame_bytes_);
    max_bytes = std::min(max_bytes, (AC_TRANSPORT_MAX_PAYLOAD / frame_bytes_) * frame_bytes_);
    std::vector<uint8_t> payload;
    writeLe32(payload, static_cast<uint32_t>(max_bytes));
    framework::transport::TransportFrame response;
    auto status = manager_->sendSync(data_channel_id_, AC_TRANSPORT_AUDIO_READ, payload, response,
                                    timeout_ms == 0 ? kAudioDataTimeoutMs : timeout_ms);
    if (!status.ok()) return status;
    status = transportPayloadStatus(response, "audio read failed");
    if (!status.ok()) return status;
    frame = std::move(response.payload);
    if (frame_bytes_ != 0) frames_read_ += frame.size() / frame_bytes_;
    return drivers::audio::AudioResult::success();
  }

  drivers::audio::AudioResult drain() {
    if (!running_ || !manager_) return drivers::audio::AudioResult::unavailable("rv32qemu playback stream is not running");
    framework::transport::TransportFrame response;
    auto status = manager_->sendSync(data_channel_id_, AC_TRANSPORT_AUDIO_DRAIN, {}, response, kAudioDataTimeoutMs);
    if (!status.ok()) return status;
    return transportPayloadStatus(response, "audio drain failed");
  }

  drivers::audio::AudioResult stop() {
    if (!open_ || !manager_) return drivers::audio::AudioResult::success();
    if (running_) {
      std::vector<uint8_t> payload;
      writeLe32(payload, stream_id_);
      framework::transport::TransportFrame response;
      auto status = manager_->sendSync(AC_TRANSPORT_CHANNEL_AUDIO_CONTROL, AC_TRANSPORT_AUDIO_STOP,
                                      payload, response, kAudioControlTimeoutMs);
      if (!status.ok()) return status;
      status = transportPayloadStatus(response, "audio stop failed");
      if (!status.ok()) return status;
      if (data_channel_id_ != 0) (void)manager_->closeChannel(data_channel_id_);
    }
    running_ = false;
    data_channel_id_ = 0;
    return drivers::audio::AudioResult::success();
  }

  void close() {
    if (manager_) {
      if (running_) (void)stop();
      if (data_channel_id_ != 0) {
        (void)manager_->closeChannel(data_channel_id_);
        data_channel_id_ = 0;
      }
      if (stream_id_ != 0) {
        std::vector<uint8_t> payload;
        writeLe32(payload, stream_id_);
        framework::transport::TransportFrame response;
        (void)manager_->sendSync(AC_TRANSPORT_CHANNEL_AUDIO_CONTROL, AC_TRANSPORT_AUDIO_CLOSE,
                                 payload, response, kAudioControlTimeoutMs);
      }
      if (control_channel_acquired_) {
        releaseAudioControlChannel(*manager_);
        control_channel_acquired_ = false;
      }
      manager_ = nullptr;
    }
    open_ = false;
    prepared_ = false;
    running_ = false;
    stream_id_ = 0;
    data_channel_id_ = 0;
    frame_bytes_ = 0;
  }

  drivers::audio::AudioStreamStats stats() const {
    return {frames_written_, frames_read_, running_};
  }

private:
  uint32_t direction_ = 0;
  drivers::audio::AudioStreamParams params_;
  std::string stream_name_;
  uint32_t stream_id_ = 0;
  uint16_t data_channel_id_ = 0;
  size_t frame_bytes_ = 0;
  size_t frames_written_ = 0;
  size_t frames_read_ = 0;
  bool open_ = false;
  bool prepared_ = false;
  bool running_ = false;
  bool control_channel_acquired_ = false;
  framework::transport::TransportManager* manager_ = nullptr;
};

class Rv32QemuPlaybackDevice final : public drivers::audio::IAudioPlaybackDevice {
public:
  drivers::audio::AudioResult open(const drivers::audio::AudioOpenParams& params) override {
    return session_.open(params);
  }
  drivers::audio::AudioResult prepare(const drivers::audio::AudioStreamParams& params) override {
    return session_.prepare(params);
  }
  drivers::audio::AudioResult start() override { return session_.start(); }
  drivers::audio::AudioResult writeFrame(const drivers::audio::AudioFrame& frame, uint32_t timeout_ms) override {
    return session_.writeFrame(frame, timeout_ms);
  }
  drivers::audio::AudioResult drain() override { return session_.drain(); }
  drivers::audio::AudioResult stop() override { return session_.stop(); }
  void close() override { session_.close(); }
  drivers::audio::AudioStreamStats getStats() const override { return session_.stats(); }
  drivers::audio::AudioDeviceCaps getCaps() const override { return {}; }

private:
  Rv32QemuAudioSession session_{AC_TRANSPORT_AUDIO_DIRECTION_PLAYBACK};
};

class Rv32QemuCaptureDevice final : public drivers::audio::IAudioCaptureDevice {
public:
  drivers::audio::AudioResult open(const drivers::audio::AudioOpenParams& params) override {
    return session_.open(params);
  }
  drivers::audio::AudioResult prepare(const drivers::audio::AudioStreamParams& params) override {
    return session_.prepare(params);
  }
  drivers::audio::AudioResult start() override { return session_.start(); }
  drivers::audio::AudioResult readFrame(drivers::audio::AudioFrame& frame, uint32_t timeout_ms) override {
    return session_.readFrame(frame, timeout_ms);
  }
  drivers::audio::AudioResult stop() override { return session_.stop(); }
  void close() override { session_.close(); }
  drivers::audio::AudioStreamStats getStats() const override { return session_.stats(); }
  drivers::audio::AudioDeviceCaps getCaps() const override { return {}; }

private:
  Rv32QemuAudioSession session_{AC_TRANSPORT_AUDIO_DIRECTION_CAPTURE};
};

class Rv32QemuPlaybackFactory final : public drivers::audio::IAudioPlaybackDeviceFactory {
public:
  explicit Rv32QemuPlaybackFactory(std::string name) : name_(std::move(name)) {}
  std::string name() const override { return name_; }
  drivers::audio::AudioResult create(const drivers::audio::AudioOpenParams& params,
                                     std::unique_ptr<drivers::audio::IAudioPlaybackDevice>& out) const override {
    auto device = std::make_unique<Rv32QemuPlaybackDevice>();
    auto status = device->open(params);
    if (!status.ok()) return status;
    out = std::move(device);
    return drivers::audio::AudioResult::success();
  }

private:
  std::string name_;
};

class Rv32QemuCaptureFactory final : public drivers::audio::IAudioCaptureDeviceFactory {
public:
  explicit Rv32QemuCaptureFactory(std::string name) : name_(std::move(name)) {}
  std::string name() const override { return name_; }
  drivers::audio::AudioResult create(const drivers::audio::AudioOpenParams& params,
                                     std::unique_ptr<drivers::audio::IAudioCaptureDevice>& out) const override {
    auto device = std::make_unique<Rv32QemuCaptureDevice>();
    auto status = device->open(params);
    if (!status.ok()) return status;
    out = std::move(device);
    return drivers::audio::AudioResult::success();
  }

private:
  std::string name_;
};

const bool kRv32QemuAudioDeviceRegistered = [] {
  auto status = drivers::audio::AudioDeviceRegistry::instance().registerPlaybackFactory(
    std::make_unique<Rv32QemuPlaybackFactory>("rv32qemu"));
  (void)status;
  status = drivers::audio::AudioDeviceRegistry::instance().registerCaptureFactory(
    std::make_unique<Rv32QemuCaptureFactory>("rv32qemu"));
  (void)status;
  status = drivers::audio::AudioDeviceRegistry::instance().registerPlaybackFactory(
    std::make_unique<Rv32QemuPlaybackFactory>("rv32qemu-simulator"));
  (void)status;
  status = drivers::audio::AudioDeviceRegistry::instance().registerCaptureFactory(
    std::make_unique<Rv32QemuCaptureFactory>("rv32qemu-simulator"));
  (void)status;
  return true;
}();

} // namespace
} // namespace audio_studio::platform::simulator
