#include "macos_core_audio_device.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

namespace audio_studio::drivers::audio {

namespace {

class MacOsCoreAudioPlaybackDeviceFactory final : public IAudioPlaybackDeviceFactory {
public:
  std::string name() const override { return "macos"; }
  AudioResult create(const AudioOpenParams& params, std::unique_ptr<IAudioPlaybackDevice>& out) const override {
    out.reset();
    auto device = std::make_unique<MacOsCoreAudioPlaybackDevice>();
    auto status = device->open(params);
    if (!status.ok()) return status;
    out = std::move(device);
    return AudioResult::success();
  }
};

class MacOsCoreAudioCaptureDeviceFactory final : public IAudioCaptureDeviceFactory {
public:
  std::string name() const override { return "macos"; }
  AudioResult create(const AudioOpenParams& params, std::unique_ptr<IAudioCaptureDevice>& out) const override {
    out.reset();
    auto device = std::make_unique<MacOsCoreAudioCaptureDevice>();
    auto status = device->open(params);
    if (!status.ok()) return status;
    out = std::move(device);
    return AudioResult::success();
  }
};

const bool kMacOsCoreAudioPlaybackDeviceRegistered = [] {
  auto status = AudioDeviceRegistry::instance().registerPlaybackFactory(std::make_unique<MacOsCoreAudioPlaybackDeviceFactory>());
  (void)status;
  return true;
}();

const bool kMacOsCoreAudioCaptureDeviceRegistered = [] {
  auto status = AudioDeviceRegistry::instance().registerCaptureFactory(std::make_unique<MacOsCoreAudioCaptureDeviceFactory>());
  (void)status;
  return true;
}();

AudioResult validateParams(const AudioStreamParams& params) {
  if (params.sample_rate == 0) return AudioResult::invalidArgument("audio sample rate is zero");
  if (params.channels == 0) return AudioResult::invalidArgument("audio channels is zero");
  if (params.bytes_per_sample == 0) return AudioResult::invalidArgument("audio bytes per sample is zero");
  if (params.bytes_per_sample != 1 && params.bytes_per_sample != 2 && params.bytes_per_sample != 3 && params.bytes_per_sample != 4) {
    return AudioResult::invalidArgument("CoreAudio PCM supports 1, 2, 3, or 4 bytes per sample");
  }
  return AudioResult::success();
}

AudioResult coreAudioError(const std::string& operation, OSStatus error) {
  return AudioResult::unavailable(operation + " failed: OSStatus " + std::to_string(error));
}

AudioStreamBasicDescription makeStreamDescription(const AudioStreamParams& params) {
  AudioStreamBasicDescription desc{};
  desc.mSampleRate = static_cast<Float64>(params.sample_rate);
  desc.mFormatID = kAudioFormatLinearPCM;
  // Use native endian and signed integer for multi-byte formats
  desc.mFormatFlags = kAudioFormatFlagIsPacked;
  if (params.bytes_per_sample > 1) {
    desc.mFormatFlags |= kAudioFormatFlagIsSignedInteger;
    // Use native endian (little-endian on modern macOS)
  }
  desc.mBytesPerPacket = static_cast<UInt32>(params.channels) * params.bytes_per_sample;
  desc.mFramesPerPacket = 1;
  desc.mBytesPerFrame = desc.mBytesPerPacket;
  desc.mChannelsPerFrame = params.channels;
  desc.mBitsPerChannel = static_cast<UInt32>(params.bytes_per_sample) * 8;
  return desc;
}

} // namespace

// ============================================================================
// Playback Device
// ============================================================================

MacOsCoreAudioPlaybackDevice::~MacOsCoreAudioPlaybackDevice() {
  close();
}

AudioResult MacOsCoreAudioPlaybackDevice::open(const AudioOpenParams& params) {
  close();
  device_name_ = params.device_name;
  blocking_write_ = params.blocking_write;
  prepared_ = false;
  running_ = false;
  return AudioResult::success();
}

AudioResult MacOsCoreAudioPlaybackDevice::prepare(const AudioStreamParams& params) {
  if (device_name_.empty()) return AudioResult::unavailable("audio playback device is not open");
  auto status = validateParams(params);
  if (!status.ok()) return status;

  close();
  params_ = params;
  format_ = makeStreamDescription(params);
  frame_bytes_ = static_cast<size_t>(params.channels) * params.bytes_per_sample;

  status = configureAudioQueue();
  if (!status.ok()) {
    close();
    return status;
  }

  prepared_ = true;
  return AudioResult::success();
}

AudioResult MacOsCoreAudioPlaybackDevice::configureAudioQueue() {
  OSStatus result = AudioQueueNewOutput(&format_,
                                         audioQueueOutputCallback,
                                         this,
                                         nullptr,  // run loop
                                         nullptr,  // run loop mode
                                         0,        // flags
                                         &audio_queue_);
  if (result != noErr) return coreAudioError("AudioQueueNewOutput", result);

  // Allocate and enqueue buffers
  const size_t buffer_size = frame_bytes_ * kBufferSizeFrames;
  for (size_t i = 0; i < kBufferCount; ++i) {
    AudioQueueBufferRef buffer = nullptr;
    result = AudioQueueAllocateBuffer(audio_queue_, static_cast<UInt32>(buffer_size), &buffer);
    if (result != noErr) return coreAudioError("AudioQueueAllocateBuffer", result);
    // Zero-fill and enqueue the buffer initially
    std::memset(buffer->mAudioData, 0, buffer_size);
    buffer->mAudioDataByteSize = static_cast<UInt32>(buffer_size);
    AudioQueueEnqueueBuffer(audio_queue_, buffer, 0, nullptr);
  }

  return AudioResult::success();
}

void MacOsCoreAudioPlaybackDevice::audioQueueOutputCallback(void* user_data,
                                                             AudioQueueRef queue,
                                                             AudioQueueBufferRef buffer) {
  // When the queue needs more data, we zero-fill the buffer to avoid glitches.
  // Actual data is written through writeFrame; this callback handles the case
  // where the queue runs ahead of the writer.
  std::memset(buffer->mAudioData, 0, buffer->mAudioDataByteSize);
  AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
}

AudioResult MacOsCoreAudioPlaybackDevice::start() {
  if (!prepared_) return AudioResult::unavailable("audio playback device is not prepared");
  if (audio_queue_ == nullptr) return AudioResult::unavailable("audio playback AudioQueue is not configured");

  OSStatus result = AudioQueueStart(audio_queue_, nullptr);
  if (result != noErr) return coreAudioError("AudioQueueStart playback", result);
  running_ = true;
  return AudioResult::success();
}

AudioResult MacOsCoreAudioPlaybackDevice::writeFrame(const AudioFrame& frame, uint32_t /*timeout_ms*/) {
  if (!running_) return AudioResult::unavailable("audio playback device is not running");
  if (audio_queue_ == nullptr) return AudioResult::unavailable("audio playback AudioQueue is not open");
  if (frame.empty()) return AudioResult::invalidArgument("audio frame is empty");
  if (frame_bytes_ == 0 || frame.size() % frame_bytes_ != 0) return AudioResult::invalidArgument("audio frame size is not aligned to sample frame");

  // Use AudioQueue offline render or direct buffer fill approach:
  // For simplicity and reliability, we use AudioQueueFreeBuffer + re-enqueue
  // pattern. However, AudioQueue's design expects the callback to provide data.
  // We instead directly write to the audio hardware via AudioQueueSetParameter
  // volume and accept that the callback fills silence when no new data arrives.

  // The simplest working approach: directly enqueue audio data as a new buffer
  const UInt32 buffer_size = static_cast<UInt32>(frame.size());
  AudioQueueBufferRef buffer = nullptr;
  OSStatus result = AudioQueueAllocateBuffer(audio_queue_, buffer_size, &buffer);
  if (result != noErr) return coreAudioError("AudioQueueAllocateBuffer write", result);

  std::memcpy(buffer->mAudioData, frame.data(), frame.size());
  buffer->mAudioDataByteSize = buffer_size;
  result = AudioQueueEnqueueBuffer(audio_queue_, buffer, 0, nullptr);
  if (result != noErr) {
    AudioQueueFreeBuffer(audio_queue_, buffer);
    return coreAudioError("AudioQueueEnqueueBuffer write", result);
  }

  frames_written_ += frame.size() / frame_bytes_;
  return AudioResult::success();
}

AudioResult MacOsCoreAudioPlaybackDevice::drain() {
  if (audio_queue_ == nullptr) return AudioResult::unavailable("audio playback device is not open");
  // Wait briefly for the queue to finish playing pending buffers
  OSStatus result = AudioQueueFlush(audio_queue_);
  if (result != noErr) return coreAudioError("AudioQueueFlush", result);
  return AudioResult::success();
}

AudioResult MacOsCoreAudioPlaybackDevice::stop() {
  if (audio_queue_ == nullptr) return AudioResult::unavailable("audio playback device is not open");
  OSStatus result = AudioQueueStop(audio_queue_, true);
  if (result != noErr) return coreAudioError("AudioQueueStop playback", result);
  running_ = false;
  return AudioResult::success();
}

void MacOsCoreAudioPlaybackDevice::close() {
  if (audio_queue_ != nullptr) {
    if (running_) (void)AudioQueueStop(audio_queue_, true);
    (void)AudioQueueDispose(audio_queue_, true);
    audio_queue_ = nullptr;
  }
  prepared_ = false;
  running_ = false;
  frame_bytes_ = 0;
}

AudioStreamStats MacOsCoreAudioPlaybackDevice::getStats() const {
  return {frames_written_, 0, running_};
}

AudioDeviceCaps MacOsCoreAudioPlaybackDevice::getCaps() const {
  return {};
}

// ============================================================================
// Capture Device
// ============================================================================

MacOsCoreAudioCaptureDevice::~MacOsCoreAudioCaptureDevice() {
  close();
}

AudioResult MacOsCoreAudioCaptureDevice::open(const AudioOpenParams& params) {
  close();
  device_name_ = params.device_name;
  prepared_ = false;
  running_ = false;
  return AudioResult::success();
}

AudioResult MacOsCoreAudioCaptureDevice::prepare(const AudioStreamParams& params) {
  if (device_name_.empty()) return AudioResult::unavailable("audio capture device is not open");
  auto status = validateParams(params);
  if (!status.ok()) return status;

  close();
  params_ = params;
  format_ = makeStreamDescription(params);
  frame_bytes_ = static_cast<size_t>(params.channels) * params.bytes_per_sample;

  // Initialize ring buffer for captured audio
  capture_buffer_.resize(kCaptureBufferSize, 0);
  capture_read_pos_ = 0;
  capture_write_pos_ = 0;

  status = configureAudioQueue();
  if (!status.ok()) {
    close();
    return status;
  }

  prepared_ = true;
  return AudioResult::success();
}

AudioResult MacOsCoreAudioCaptureDevice::configureAudioQueue() {
  OSStatus result = AudioQueueNewInput(&format_,
                                        audioQueueInputCallback,
                                        this,
                                        nullptr,  // run loop
                                        nullptr,  // run loop mode
                                        0,        // flags
                                        &audio_queue_);
  if (result != noErr) return coreAudioError("AudioQueueNewInput", result);

  // Allocate buffers for the input queue
  constexpr size_t kCaptureBufferSizeFrames = 1024;
  constexpr size_t kCaptureBufferCount = 3;
  const size_t buffer_size = frame_bytes_ * kCaptureBufferSizeFrames;
  for (size_t i = 0; i < kCaptureBufferCount; ++i) {
    AudioQueueBufferRef buffer = nullptr;
    result = AudioQueueAllocateBuffer(audio_queue_, static_cast<UInt32>(buffer_size), &buffer);
    if (result != noErr) return coreAudioError("AudioQueueAllocateBuffer capture", result);
    AudioQueueEnqueueBuffer(audio_queue_, buffer, 0, nullptr);
  }

  return AudioResult::success();
}

void MacOsCoreAudioCaptureDevice::audioQueueInputCallback(void* user_data,
                                                           AudioQueueRef /*queue*/,
                                                           AudioQueueBufferRef buffer,
                                                           const AudioTimeStamp* /*start_time*/,
                                                           UInt32 /*num_packets*/,
                                                           const AudioStreamPacketDescription* /*packet_desc*/) {
  auto* self = static_cast<MacOsCoreAudioCaptureDevice*>(user_data);
  if (self == nullptr || buffer == nullptr) return;

  const size_t bytes_available = buffer->mAudioDataByteSize;
  if (bytes_available == 0) return;

  const auto* src = static_cast<const uint8_t*>(buffer->mAudioData);

  // Write into the ring buffer
  for (UInt32 i = 0; i < bytes_available; ++i) {
    self->capture_buffer_[self->capture_write_pos_] = src[i];
    self->capture_write_pos_ = (self->capture_write_pos_ + 1) % self->capture_buffer_.size();
    // If write catches up to read, advance read (drop oldest data)
    if (self->capture_write_pos_ == self->capture_read_pos_) {
      self->capture_read_pos_ = (self->capture_read_pos_ + 1) % self->capture_buffer_.size();
    }
  }
}

AudioResult MacOsCoreAudioCaptureDevice::start() {
  if (!prepared_) return AudioResult::unavailable("audio capture device is not prepared");
  if (audio_queue_ == nullptr) return AudioResult::unavailable("audio capture AudioQueue is not configured");

  // Reset ring buffer
  capture_read_pos_ = 0;
  capture_write_pos_ = 0;

  OSStatus result = AudioQueueStart(audio_queue_, nullptr);
  if (result != noErr) return coreAudioError("AudioQueueStart capture", result);
  running_ = true;
  return AudioResult::success();
}

AudioResult MacOsCoreAudioCaptureDevice::readFrame(AudioFrame& frame, uint32_t timeout_ms) {
  if (!running_) return AudioResult::unavailable("audio capture device is not running");
  if (frame_bytes_ == 0) return AudioResult::unavailable("audio capture device is not configured");

  // Determine how many frames to read
  size_t requested_bytes = frame.empty() ? frame_bytes_ * 256 : frame.size();
  requested_bytes = std::max(frame_bytes_, (requested_bytes / frame_bytes_) * frame_bytes_);

  // Wait until enough data is available or timeout
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (true) {
    size_t available = 0;
    if (capture_write_pos_ >= capture_read_pos_) {
      available = capture_write_pos_ - capture_read_pos_;
    } else {
      available = capture_buffer_.size() - capture_read_pos_ + capture_write_pos_;
    }

    if (available >= requested_bytes) break;

    if (std::chrono::steady_clock::now() >= deadline) {
      if (available >= frame_bytes_) {
        // Return whatever we have, aligned to frame boundary
        requested_bytes = (available / frame_bytes_) * frame_bytes_;
        break;
      }
      return AudioResult::unavailable("audio capture wait timed out");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Read from ring buffer
  frame.resize(requested_bytes);
  for (size_t i = 0; i < requested_bytes; ++i) {
    frame[i] = capture_buffer_[capture_read_pos_];
    capture_read_pos_ = (capture_read_pos_ + 1) % capture_buffer_.size();
  }

  frames_read_ += requested_bytes / frame_bytes_;
  return AudioResult::success();
}

AudioResult MacOsCoreAudioCaptureDevice::stop() {
  if (audio_queue_ == nullptr) return AudioResult::unavailable("audio capture device is not open");
  OSStatus result = AudioQueueStop(audio_queue_, true);
  if (result != noErr) return coreAudioError("AudioQueueStop capture", result);
  running_ = false;
  return AudioResult::success();
}

void MacOsCoreAudioCaptureDevice::close() {
  if (audio_queue_ != nullptr) {
    if (running_) (void)AudioQueueStop(audio_queue_, true);
    (void)AudioQueueDispose(audio_queue_, true);
    audio_queue_ = nullptr;
  }
  prepared_ = false;
  running_ = false;
  frame_bytes_ = 0;
  capture_buffer_.clear();
  capture_read_pos_ = 0;
  capture_write_pos_ = 0;
}

AudioStreamStats MacOsCoreAudioCaptureDevice::getStats() const {
  return {0, frames_read_, running_};
}

AudioDeviceCaps MacOsCoreAudioCaptureDevice::getCaps() const {
  return {};
}

} // namespace audio_studio::drivers::audio
