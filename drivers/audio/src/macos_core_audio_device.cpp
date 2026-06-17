#include "macos_core_audio_device.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

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
  desc.mFormatFlags = kAudioFormatFlagIsPacked;
  if (params.bytes_per_sample > 1) {
    desc.mFormatFlags |= kAudioFormatFlagIsSignedInteger;
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
// Playback Device - callback-pull pattern with ring buffer
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
  stopping_ = false;
  return AudioResult::success();
}

AudioResult MacOsCoreAudioPlaybackDevice::prepare(const AudioStreamParams& params) {
  if (device_name_.empty()) return AudioResult::invalidArgument("audio playback device name is empty");
  auto status = validateParams(params);
  if (!status.ok()) return status;

  close();
  params_ = params;
  format_ = makeStreamDescription(params);
  frame_bytes_ = static_cast<size_t>(params.channels) * params.bytes_per_sample;

  // Initialize ring buffer
  ring_buffer_.resize(kRingBufferSize, 0);
  ring_read_pos_ = 0;
  ring_write_pos_ = 0;

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
                                         nullptr,
                                         nullptr,
                                         0,
                                         &audio_queue_);
  if (result != noErr) return coreAudioError("AudioQueueNewOutput", result);

  // Allocate and enqueue buffers - they will be filled by callback
  const size_t buffer_size = frame_bytes_ * kBufferSizeFrames;
  for (size_t i = 0; i < kBufferCount; ++i) {
    AudioQueueBufferRef buffer = nullptr;
    result = AudioQueueAllocateBuffer(audio_queue_, static_cast<UInt32>(buffer_size), &buffer);
    if (result != noErr) return coreAudioError("AudioQueueAllocateBuffer", result);
    buffer->mAudioDataByteSize = static_cast<UInt32>(buffer_size);
    AudioQueueEnqueueBuffer(audio_queue_, buffer, 0, nullptr);
  }

  return AudioResult::success();
}

size_t MacOsCoreAudioPlaybackDevice::ringAvailable() const {
  if (ring_write_pos_ >= ring_read_pos_) {
    return ring_buffer_.size() - ring_write_pos_ + ring_read_pos_ - 1;
  } else {
    return ring_read_pos_ - ring_write_pos_ - 1;
  }
}

size_t MacOsCoreAudioPlaybackDevice::ringUsed() const {
  if (ring_write_pos_ >= ring_read_pos_) {
    return ring_write_pos_ - ring_read_pos_;
  } else {
    return ring_buffer_.size() - ring_read_pos_ + ring_write_pos_;
  }
}

void MacOsCoreAudioPlaybackDevice::audioQueueOutputCallback(void* user_data,
                                                             AudioQueueRef queue,
                                                             AudioQueueBufferRef buffer) {
  auto* self = static_cast<MacOsCoreAudioPlaybackDevice*>(user_data);
  if (self == nullptr || buffer == nullptr) {
    // Fill with silence and re-enqueue
    std::memset(buffer->mAudioData, 0, buffer->mAudioDataByteSize);
    AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
    return;
  }

  std::lock_guard<std::mutex> lock(self->ring_mutex_);

  if (self->stopping_ || !self->running_) {
    // Stopping: fill with silence
    std::memset(buffer->mAudioData, 0, buffer->mAudioDataByteSize);
    AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
    return;
  }

  // Pull data from ring buffer
  auto* dst = static_cast<uint8_t*>(buffer->mAudioData);
  const size_t to_copy = buffer->mAudioDataByteSize;
  size_t copied = 0;

  while (copied < to_copy && self->ringUsed() > 0) {
    const size_t chunk = std::min<size_t>(to_copy - copied,
                                           self->ring_buffer_.size() - self->ring_read_pos_);
    std::memcpy(dst + copied, &self->ring_buffer_[self->ring_read_pos_], chunk);
    self->ring_read_pos_ = (self->ring_read_pos_ + chunk) % self->ring_buffer_.size();
    copied += chunk;
  }

  // Fill remainder with silence
  if (copied < to_copy) {
    std::memset(dst + copied, 0, to_copy - copied);
  }

  // Notify writer that ring buffer has space
  self->ring_not_full_.notify_one();

  // Re-enqueue buffer for continuous playback
  AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
}

AudioResult MacOsCoreAudioPlaybackDevice::start() {
  if (!prepared_) return AudioResult::unavailable("audio playback device is not prepared");
  if (audio_queue_ == nullptr) return AudioResult::unavailable("audio playback AudioQueue is not configured");

  stopping_ = false;
  OSStatus result = AudioQueueStart(audio_queue_, nullptr);
  if (result != noErr) return coreAudioError("AudioQueueStart playback", result);
  running_ = true;
  return AudioResult::success();
}

AudioResult MacOsCoreAudioPlaybackDevice::writeFrame(const AudioFrame& frame, uint32_t timeout_ms) {
  if (!running_) return AudioResult::unavailable("audio playback device is not running");
  if (audio_queue_ == nullptr) return AudioResult::unavailable("audio playback AudioQueue is not open");
  if (frame.empty()) return AudioResult::invalidArgument("audio frame is empty");
  if (frame_bytes_ == 0 || frame.size() % frame_bytes_ != 0) return AudioResult::invalidArgument("audio frame size is not aligned to sample frame");

  const size_t bytes_to_write = frame.size();

  std::unique_lock<std::mutex> lock(ring_mutex_);

  // Wait until there's enough space in ring buffer (blocking mode) or return immediately (non-blocking)
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  while (ringAvailable() < bytes_to_write) {
    if (!blocking_write_ || !running_ || stopping_) {
      return AudioResult::unavailable("audio playback ring buffer full");
    }
    if (timeout_ms == 0) {
      return AudioResult::unavailable("audio playback ring buffer full, no wait");
    }
    if (ring_not_full_.wait_until(lock, deadline) == std::cv_status::timeout) {
      return AudioResult::unavailable("audio playback wait timed out");
    }
  }

  // Write data to ring buffer
  const auto* src = frame.data();
  size_t written = 0;
  while (written < bytes_to_write) {
    const size_t chunk = std::min<size_t>(bytes_to_write - written,
                                           ring_buffer_.size() - ring_write_pos_);
    std::memcpy(&ring_buffer_[ring_write_pos_], src + written, chunk);
    ring_write_pos_ = (ring_write_pos_ + chunk) % ring_buffer_.size();
    written += chunk;
  }

  // Notify callback that data is available
  ring_not_empty_.notify_one();

  frames_written_ += bytes_to_write / frame_bytes_;
  return AudioResult::success();
}

AudioResult MacOsCoreAudioPlaybackDevice::drain() {
  if (audio_queue_ == nullptr) return AudioResult::unavailable("audio playback device is not open");

  // Wait until ring buffer is empty
  std::unique_lock<std::mutex> lock(ring_mutex_);
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);

  while (ringUsed() > 0 && running_) {
    if (ring_not_full_.wait_until(lock, deadline) == std::cv_status::timeout) {
      break;
    }
  }

  // Flush AudioQueue
  OSStatus result = AudioQueueFlush(audio_queue_);
  if (result != noErr) return coreAudioError("AudioQueueFlush", result);
  return AudioResult::success();
}

AudioResult MacOsCoreAudioPlaybackDevice::stop() {
  if (audio_queue_ == nullptr) return AudioResult::unavailable("audio playback device is not open");

  stopping_ = true;
  OSStatus result = AudioQueueStop(audio_queue_, true);
  if (result != noErr) return coreAudioError("AudioQueueStop playback", result);
  running_ = false;
  stopping_ = false;
  return AudioResult::success();
}

void MacOsCoreAudioPlaybackDevice::close() {
  stopping_ = true;
  if (audio_queue_ != nullptr) {
    if (running_) (void)AudioQueueStop(audio_queue_, true);
    (void)AudioQueueDispose(audio_queue_, true);
    audio_queue_ = nullptr;
  }
  prepared_ = false;
  running_ = false;
  stopping_ = false;
  frame_bytes_ = 0;
  ring_buffer_.clear();
  ring_read_pos_ = 0;
  ring_write_pos_ = 0;
}

AudioStreamStats MacOsCoreAudioPlaybackDevice::getStats() const {
  return {frames_written_, 0, running_};
}

AudioDeviceCaps MacOsCoreAudioPlaybackDevice::getCaps() const {
  return {};
}

// ============================================================================
// Capture Device - callback-push pattern with synchronized ring buffer
// ============================================================================

MacOsCoreAudioCaptureDevice::~MacOsCoreAudioCaptureDevice() {
  close();
}

AudioResult MacOsCoreAudioCaptureDevice::open(const AudioOpenParams& params) {
  close();
  device_name_ = params.device_name;
  prepared_ = false;
  running_ = false;
  stopping_ = false;
  return AudioResult::success();
}

AudioResult MacOsCoreAudioCaptureDevice::prepare(const AudioStreamParams& params) {
  if (device_name_.empty()) return AudioResult::invalidArgument("audio capture device name is empty");
  auto status = validateParams(params);
  if (!status.ok()) return status;

  close();
  params_ = params;
  format_ = makeStreamDescription(params);
  frame_bytes_ = static_cast<size_t>(params.channels) * params.bytes_per_sample;

  // Initialize ring buffer
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
                                        nullptr,
                                        nullptr,
                                        0,
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

size_t MacOsCoreAudioCaptureDevice::captureAvailable() const {
  if (capture_write_pos_ >= capture_read_pos_) {
    return capture_buffer_.size() - capture_write_pos_ + capture_read_pos_ - 1;
  } else {
    return capture_read_pos_ - capture_write_pos_ - 1;
  }
}

void MacOsCoreAudioCaptureDevice::audioQueueInputCallback(void* user_data,
                                                           AudioQueueRef queue,
                                                           AudioQueueBufferRef buffer,
                                                           const AudioTimeStamp* /*start_time*/,
                                                           UInt32 /*num_packets*/,
                                                           const AudioStreamPacketDescription* /*packet_desc*/) {
  auto* self = static_cast<MacOsCoreAudioCaptureDevice*>(user_data);
  if (self == nullptr || buffer == nullptr) return;

  const size_t bytes_available = buffer->mAudioDataByteSize;
  if (bytes_available == 0) return;

  std::unique_lock<std::mutex> lock(self->capture_mutex_);

  if (self->stopping_ || !self->running_) {
    // Don't re-enqueue when stopping
    return;
  }

  const auto* src = static_cast<const uint8_t*>(buffer->mAudioData);

  // Write into the ring buffer
  size_t written = 0;
  while (written < bytes_available && self->captureAvailable() > 0) {
    const size_t chunk = std::min<size_t>(bytes_available - written,
                                           self->capture_buffer_.size() - self->capture_write_pos_);
    std::memcpy(&self->capture_buffer_[self->capture_write_pos_], src + written, chunk);
    self->capture_write_pos_ = (self->capture_write_pos_ + chunk) % self->capture_buffer_.size();
    written += chunk;
  }

  // Notify reader
  self->capture_cv_.notify_one();

  // Re-enqueue buffer for continuous capture
  if (self->running_ && !self->stopping_) {
    AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
  }
}

AudioResult MacOsCoreAudioCaptureDevice::start() {
  if (!prepared_) return AudioResult::unavailable("audio capture device is not prepared");
  if (audio_queue_ == nullptr) return AudioResult::unavailable("audio capture AudioQueue is not configured");

  // Reset ring buffer
  std::lock_guard<std::mutex> lock(capture_mutex_);
  capture_read_pos_ = 0;
  capture_write_pos_ = 0;
  stopping_ = false;

  OSStatus result = AudioQueueStart(audio_queue_, nullptr);
  if (result != noErr) return coreAudioError("AudioQueueStart capture", result);
  running_ = true;
  return AudioResult::success();
}

AudioResult MacOsCoreAudioCaptureDevice::readFrame(AudioFrame& frame, uint32_t timeout_ms) {
  if (!running_) return AudioResult::unavailable("audio capture device is not running");
  if (frame_bytes_ == 0) return AudioResult::unavailable("audio capture device is not configured");

  // Determine how many bytes to read
  size_t requested_bytes = frame.empty() ? frame_bytes_ * 256 : frame.size();
  requested_bytes = std::max(frame_bytes_, (requested_bytes / frame_bytes_) * frame_bytes_);

  std::unique_lock<std::mutex> lock(capture_mutex_);
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  // Wait until enough data is available
  size_t available = 0;
  while (true) {
    if (capture_write_pos_ >= capture_read_pos_) {
      available = capture_write_pos_ - capture_read_pos_;
    } else {
      available = capture_buffer_.size() - capture_read_pos_ + capture_write_pos_;
    }

    if (available >= requested_bytes) break;

    if (stopping_ || !running_) {
      // Return whatever we have
      requested_bytes = std::min<size_t>(available, (available / frame_bytes_) * frame_bytes_);
      if (requested_bytes == 0) return AudioResult::unavailable("audio capture stopped");
      break;
    }

    if (capture_cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
      requested_bytes = std::min<size_t>(available, (available / frame_bytes_) * frame_bytes_);
      if (requested_bytes == 0) return AudioResult::unavailable("audio capture wait timed out");
      break;
    }
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

  stopping_ = true;
  OSStatus result = AudioQueueStop(audio_queue_, true);
  if (result != noErr) return coreAudioError("AudioQueueStop capture", result);

  // Wake up any waiting reader
  capture_cv_.notify_all();

  running_ = false;
  stopping_ = false;
  return AudioResult::success();
}

void MacOsCoreAudioCaptureDevice::close() {
  stopping_ = true;

  if (audio_queue_ != nullptr) {
    if (running_) {
      (void)AudioQueueStop(audio_queue_, true);
    }
    (void)AudioQueueDispose(audio_queue_, true);
    audio_queue_ = nullptr;
  }

  // Wake up any waiting reader before clearing buffer
  capture_cv_.notify_all();

  std::lock_guard<std::mutex> lock(capture_mutex_);
  prepared_ = false;
  running_ = false;
  stopping_ = false;
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