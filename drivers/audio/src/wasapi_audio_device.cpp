#include "wasapi_audio_device.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>

#include <initguid.h>

namespace audio_studio::drivers::audio {
namespace {

constexpr REFERENCE_TIME kDefaultBufferDuration = 1000000; // 100 ms

class WasapiAudioPlaybackDeviceFactory final : public IAudioPlaybackDeviceFactory {
public:
  std::string name() const override { return "wasapi"; }
  AudioResult create(const AudioOpenParams& params, std::unique_ptr<IAudioPlaybackDevice>& out) const override {
    out.reset();
    auto device = std::make_unique<WasapiAudioPlaybackDevice>();
    auto status = device->open(params);
    if (!status.ok()) return status;
    out = std::move(device);
    return AudioResult::success();
  }
};

class WasapiAudioCaptureDeviceFactory final : public IAudioCaptureDeviceFactory {
public:
  std::string name() const override { return "wasapi"; }
  AudioResult create(const AudioOpenParams& params, std::unique_ptr<IAudioCaptureDevice>& out) const override {
    out.reset();
    auto device = std::make_unique<WasapiAudioCaptureDevice>();
    auto status = device->open(params);
    if (!status.ok()) return status;
    out = std::move(device);
    return AudioResult::success();
  }
};

const bool kWasapiAudioPlaybackDeviceRegistered = [] {
  auto status = AudioDeviceRegistry::instance().registerPlaybackFactory(std::make_unique<WasapiAudioPlaybackDeviceFactory>());
  (void)status;
  return true;
}();

const bool kWasapiAudioCaptureDeviceRegistered = [] {
  auto status = AudioDeviceRegistry::instance().registerCaptureFactory(std::make_unique<WasapiAudioCaptureDeviceFactory>());
  (void)status;
  return true;
}();

struct ComApartment {
  ComApartment() {
    hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    initialized = hr == S_OK || hr == S_FALSE;
  }

  ~ComApartment() {
    if (initialized) CoUninitialize();
  }

  HRESULT hr = S_OK;
  bool initialized = false;
};

AudioResult ensureCom() {
  static thread_local ComApartment apartment;
  if (apartment.hr == RPC_E_CHANGED_MODE) return AudioResult::success();
  if (FAILED(apartment.hr)) {
    std::ostringstream out;
    out << "CoInitializeEx failed: HRESULT 0x" << std::hex << static_cast<unsigned long>(apartment.hr);
    return AudioResult::unavailable(out.str());
  }
  return AudioResult::success();
}

AudioResult hresultError(const std::string& operation, HRESULT hr) {
  std::ostringstream out;
  out << operation << " failed: HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
  return AudioResult::unavailable(out.str());
}

template <typename T>
void releaseCom(T*& value) {
  if (value != nullptr) {
    value->Release();
    value = nullptr;
  }
}

AudioResult validateParams(const AudioStreamParams& params) {
  if (params.sample_rate == 0) return AudioResult::invalidArgument("audio sample rate is zero");
  if (params.channels == 0) return AudioResult::invalidArgument("audio channels is zero");
  if (params.bytes_per_sample == 0) return AudioResult::invalidArgument("audio bytes per sample is zero");
  if (params.bytes_per_sample != 1 && params.bytes_per_sample != 2 && params.bytes_per_sample != 4) {
    return AudioResult::invalidArgument("WASAPI PCM path supports 1, 2, or 4 bytes per sample");
  }
  return AudioResult::success();
}

WAVEFORMATEX waveFormat(const AudioStreamParams& params) {
  WAVEFORMATEX format {};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = params.channels;
  format.nSamplesPerSec = params.sample_rate;
  format.wBitsPerSample = static_cast<WORD>(params.bytes_per_sample * 8);
  format.nBlockAlign = static_cast<WORD>(params.channels * params.bytes_per_sample);
  format.nAvgBytesPerSec = params.sample_rate * format.nBlockAlign;
  format.cbSize = 0;
  return format;
}

AudioResult createDeviceEnumerator(IMMDeviceEnumerator*& enumerator) {
  auto status = ensureCom();
  if (!status.ok()) return status;
  const HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                      nullptr,
                                      CLSCTX_ALL,
                                      __uuidof(IMMDeviceEnumerator),
                                      reinterpret_cast<void**>(&enumerator));
  if (FAILED(hr)) return hresultError("CoCreateInstance(MMDeviceEnumerator)", hr);
  return AudioResult::success();
}

AudioResult openEndpoint(EDataFlow flow, const std::string& device_name, IMMDevice*& device) {
  IMMDeviceEnumerator* enumerator = nullptr;
  auto status = createDeviceEnumerator(enumerator);
  if (!status.ok()) return status;

  HRESULT hr = S_OK;
  if (device_name.empty() || device_name == "default") {
    hr = enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device);
  } else {
    std::wstring id(device_name.begin(), device_name.end());
    hr = enumerator->GetDevice(id.c_str(), &device);
  }
  enumerator->Release();
  if (FAILED(hr)) return hresultError(flow == eRender ? "Get WASAPI render endpoint" : "Get WASAPI capture endpoint", hr);
  return AudioResult::success();
}

AudioResult initializeAudioClient(EDataFlow flow,
                                  const std::string& device_name,
                                  const AudioStreamParams& params,
                                  IAudioClient*& audio_client,
                                  UINT32& buffer_frames,
                                  size_t& frame_bytes) {
  auto status = validateParams(params);
  if (!status.ok()) return status;
  status = ensureCom();
  if (!status.ok()) return status;

  IMMDevice* device = nullptr;
  status = openEndpoint(flow, device_name, device);
  if (!status.ok()) return status;

  HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&audio_client));
  device->Release();
  if (FAILED(hr)) return hresultError("IMMDevice::Activate(IAudioClient)", hr);

  WAVEFORMATEX format = waveFormat(params);
  WAVEFORMATEX* closest = nullptr;
  hr = audio_client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &format, &closest);
  if (closest != nullptr) CoTaskMemFree(closest);
  if (FAILED(hr)) return hresultError("IAudioClient::IsFormatSupported", hr);
  if (hr == S_FALSE) return AudioResult::unavailable("WASAPI shared-mode endpoint does not support requested PCM format");

  hr = audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, kDefaultBufferDuration, 0, &format, nullptr);
  if (FAILED(hr)) return hresultError("IAudioClient::Initialize", hr);
  hr = audio_client->GetBufferSize(&buffer_frames);
  if (FAILED(hr)) return hresultError("IAudioClient::GetBufferSize", hr);

  frame_bytes = static_cast<size_t>(params.channels) * params.bytes_per_sample;
  return AudioResult::success();
}

bool timedOut(DWORD started_ms, uint32_t timeout_ms) {
  if (timeout_ms == std::numeric_limits<uint32_t>::max()) return false;
  return static_cast<DWORD>(GetTickCount() - started_ms) >= timeout_ms;
}

} // namespace

WasapiAudioPlaybackDevice::~WasapiAudioPlaybackDevice() {
  close();
}

AudioResult WasapiAudioPlaybackDevice::open(const AudioOpenParams& params) {
  close();
  device_name_ = params.device_name;
  blocking_write_ = params.blocking_write;
  return AudioResult::success();
}

AudioResult WasapiAudioPlaybackDevice::prepare(const AudioStreamParams& params) {
  auto status = validateParams(params);
  if (!status.ok()) return status;
  releaseClient();
  params_ = params;
  frame_bytes_ = static_cast<size_t>(params.channels) * params.bytes_per_sample;
  prepared_ = true;
  return AudioResult::success();
}

AudioResult WasapiAudioPlaybackDevice::start() {
  if (!prepared_) return AudioResult::unavailable("WASAPI playback device is not prepared");
  running_ = true;
  return AudioResult::success();
}

AudioResult WasapiAudioPlaybackDevice::ensureStarted() {
  if (client_started_) return AudioResult::success();
  releaseClient();
  auto status = initializeAudioClient(eRender, device_name_, params_, audio_client_, buffer_frames_, frame_bytes_);
  if (!status.ok()) return status;
  HRESULT hr = audio_client_->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&render_client_));
  if (FAILED(hr)) return hresultError("IAudioClient::GetService(IAudioRenderClient)", hr);
  hr = audio_client_->Start();
  if (FAILED(hr)) return hresultError("IAudioClient::Start playback", hr);
  client_started_ = true;
  return AudioResult::success();
}

AudioResult WasapiAudioPlaybackDevice::writeFrame(const AudioFrame& frame, uint32_t timeout_ms) {
  if (!running_) return AudioResult::unavailable("WASAPI playback device is not running");
  if (frame.empty()) return AudioResult::invalidArgument("audio frame is empty");
  if (frame_bytes_ == 0 || frame.size() % frame_bytes_ != 0) return AudioResult::invalidArgument("audio frame size is not aligned to sample frame");
  auto status = ensureStarted();
  if (!status.ok()) return status;

  const BYTE* source = frame.data();
  UINT32 frames_remaining = static_cast<UINT32>(frame.size() / frame_bytes_);
  const DWORD started = GetTickCount();
  while (frames_remaining > 0) {
    UINT32 padding = 0;
    HRESULT hr = audio_client_->GetCurrentPadding(&padding);
    if (FAILED(hr)) return hresultError("IAudioClient::GetCurrentPadding", hr);
    const UINT32 available = buffer_frames_ > padding ? buffer_frames_ - padding : 0;
    if (available == 0) {
      if (timedOut(started, timeout_ms)) return AudioResult::unavailable("WASAPI playback wait timed out");
      Sleep(5);
      continue;
    }

    const UINT32 frames_to_write = std::min(available, frames_remaining);
    BYTE* destination = nullptr;
    hr = render_client_->GetBuffer(frames_to_write, &destination);
    if (FAILED(hr)) return hresultError("IAudioRenderClient::GetBuffer", hr);
    std::memcpy(destination, source, static_cast<size_t>(frames_to_write) * frame_bytes_);
    hr = render_client_->ReleaseBuffer(frames_to_write, 0);
    if (FAILED(hr)) return hresultError("IAudioRenderClient::ReleaseBuffer", hr);

    source += static_cast<size_t>(frames_to_write) * frame_bytes_;
    frames_remaining -= frames_to_write;
    frames_written_ += frames_to_write;
  }
  return AudioResult::success();
}

AudioResult WasapiAudioPlaybackDevice::drain() {
  if (!client_started_ || audio_client_ == nullptr) return AudioResult::success();
  const DWORD started = GetTickCount();
  while (true) {
    UINT32 padding = 0;
    HRESULT hr = audio_client_->GetCurrentPadding(&padding);
    if (FAILED(hr)) return hresultError("IAudioClient::GetCurrentPadding", hr);
    if (padding == 0) return AudioResult::success();
    if (timedOut(started, 5000)) return AudioResult::unavailable("WASAPI playback drain timed out");
    Sleep(5);
  }
}

AudioResult WasapiAudioPlaybackDevice::stop() {
  if (audio_client_ != nullptr && client_started_) {
    const HRESULT hr = audio_client_->Stop();
    if (FAILED(hr)) return hresultError("IAudioClient::Stop playback", hr);
  }
  client_started_ = false;
  running_ = false;
  return AudioResult::success();
}

void WasapiAudioPlaybackDevice::releaseClient() {
  if (audio_client_ != nullptr && client_started_) (void)audio_client_->Stop();
  client_started_ = false;
  releaseCom(render_client_);
  releaseCom(audio_client_);
  buffer_frames_ = 0;
}

void WasapiAudioPlaybackDevice::close() {
  releaseClient();
  prepared_ = false;
  running_ = false;
  frame_bytes_ = 0;
}

AudioStreamStats WasapiAudioPlaybackDevice::getStats() const {
  return {frames_written_, 0, running_};
}

AudioDeviceCaps WasapiAudioPlaybackDevice::getCaps() const {
  return {};
}

WasapiAudioCaptureDevice::~WasapiAudioCaptureDevice() {
  close();
}

AudioResult WasapiAudioCaptureDevice::open(const AudioOpenParams& params) {
  close();
  device_name_ = params.device_name;
  return AudioResult::success();
}

AudioResult WasapiAudioCaptureDevice::prepare(const AudioStreamParams& params) {
  auto status = validateParams(params);
  if (!status.ok()) return status;
  releaseClient();
  params_ = params;
  frame_bytes_ = static_cast<size_t>(params.channels) * params.bytes_per_sample;
  prepared_ = true;
  return AudioResult::success();
}

AudioResult WasapiAudioCaptureDevice::start() {
  if (!prepared_) return AudioResult::unavailable("WASAPI capture device is not prepared");
  running_ = true;
  return AudioResult::success();
}

AudioResult WasapiAudioCaptureDevice::ensureStarted() {
  if (client_started_) return AudioResult::success();
  releaseClient();
  auto status = initializeAudioClient(eCapture, device_name_, params_, audio_client_, buffer_frames_, frame_bytes_);
  if (!status.ok()) return status;
  HRESULT hr = audio_client_->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture_client_));
  if (FAILED(hr)) return hresultError("IAudioClient::GetService(IAudioCaptureClient)", hr);
  hr = audio_client_->Start();
  if (FAILED(hr)) return hresultError("IAudioClient::Start capture", hr);
  client_started_ = true;
  return AudioResult::success();
}

AudioResult WasapiAudioCaptureDevice::readFrame(AudioFrame& frame, uint32_t timeout_ms) {
  if (!running_) return AudioResult::unavailable("WASAPI capture device is not running");
  auto status = ensureStarted();
  if (!status.ok()) return status;
  if (frame.empty()) frame.resize(frame_bytes_ * 256);
  const size_t requested_bytes = std::max<size_t>(frame_bytes_, (frame.size() / frame_bytes_) * frame_bytes_);
  const UINT32 requested_frames = static_cast<UINT32>(requested_bytes / frame_bytes_);
  frame.clear();
  frame.reserve(requested_bytes);

  const DWORD started = GetTickCount();
  while (frame.size() < requested_bytes) {
    UINT32 packet_frames = 0;
    HRESULT hr = capture_client_->GetNextPacketSize(&packet_frames);
    if (FAILED(hr)) return hresultError("IAudioCaptureClient::GetNextPacketSize", hr);
    if (packet_frames == 0) {
      if (timedOut(started, timeout_ms)) return AudioResult::unavailable("WASAPI capture wait timed out");
      Sleep(5);
      continue;
    }

    BYTE* data = nullptr;
    DWORD flags = 0;
    hr = capture_client_->GetBuffer(&data, &packet_frames, &flags, nullptr, nullptr);
    if (FAILED(hr)) return hresultError("IAudioCaptureClient::GetBuffer", hr);

    const UINT32 frames_to_copy = std::min<UINT32>(packet_frames, requested_frames - static_cast<UINT32>(frame.size() / frame_bytes_));
    const size_t bytes_to_copy = static_cast<size_t>(frames_to_copy) * frame_bytes_;
    const size_t old_size = frame.size();
    frame.resize(old_size + bytes_to_copy);
    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == nullptr) {
      std::fill(frame.begin() + static_cast<std::ptrdiff_t>(old_size), frame.end(), 0);
    } else {
      std::memcpy(frame.data() + old_size, data, bytes_to_copy);
    }

    hr = capture_client_->ReleaseBuffer(packet_frames);
    if (FAILED(hr)) return hresultError("IAudioCaptureClient::ReleaseBuffer", hr);
    frames_read_ += frames_to_copy;
    if (frames_to_copy == 0) break;
  }
  return AudioResult::success();
}

AudioResult WasapiAudioCaptureDevice::stop() {
  if (audio_client_ != nullptr && client_started_) {
    const HRESULT hr = audio_client_->Stop();
    if (FAILED(hr)) return hresultError("IAudioClient::Stop capture", hr);
  }
  client_started_ = false;
  running_ = false;
  return AudioResult::success();
}

void WasapiAudioCaptureDevice::releaseClient() {
  if (audio_client_ != nullptr && client_started_) (void)audio_client_->Stop();
  client_started_ = false;
  releaseCom(capture_client_);
  releaseCom(audio_client_);
  buffer_frames_ = 0;
}

void WasapiAudioCaptureDevice::close() {
  releaseClient();
  prepared_ = false;
  running_ = false;
  frame_bytes_ = 0;
}

AudioStreamStats WasapiAudioCaptureDevice::getStats() const {
  return {0, frames_read_, running_};
}

AudioDeviceCaps WasapiAudioCaptureDevice::getCaps() const {
  return {};
}

} // namespace audio_studio::drivers::audio
