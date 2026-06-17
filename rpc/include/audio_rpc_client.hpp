#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "json_rpc.hpp"
#include "rpc_stream_transport.hpp"

namespace audio_studio::rpc {

inline std::string defaultAudioDriverFactory() {
#if defined(_WIN32)
  return "wasapi";
#else
  return "alsa";
#endif
}

struct AudioSessionConfig {
  std::string session_id;
  uint32_t sample_rate = 48000;
  uint16_t channels = 2;
  uint16_t bytes_per_sample = 2;
  std::string sample_format = "s16le";
  std::string device_name = "default";
  std::string driver_factory = defaultAudioDriverFactory();
  bool blocking_write = true;
};

struct AudioStreamDescriptor {
  std::string stream_id;
  uint32_t numeric_stream_id = 0;
  std::string uri;
  std::string direction;
  std::string framing;
  std::string payload;
  uint32_t max_chunk_bytes = 65536;
  uint32_t default_timeout_ms = 5000;
  bool blocking = true;
};

struct AudioWriteOptions {
  uint32_t timeout_ms = 5000;
};

struct AudioWriteResult {
  bool accepted = false;
  uint64_t bytes = 0;
  uint64_t queued_bytes = 0;
  uint64_t credit_bytes = 0;
};

struct AudioReadOptions {
  uint32_t timeout_ms = 5000;
};

struct AudioReadResult {
  bool ok = false;
  bool eof = false;
  uint64_t bytes = 0;
  std::vector<uint8_t> data;
};

class AudioRpcClient;

class AudioPlayback {
public:
  AudioPlayback(AudioRpcClient& audio, std::string session_id, uint32_t numeric_session_id, AudioStreamDescriptor stream);

  const std::string& sessionId() const;
  const AudioStreamDescriptor& stream() const;
  JsonValue start();
  JsonValue drain();
  JsonValue stop();
  JsonValue close();
  JsonValue stats();
  AudioWriteResult writeFrames(const std::vector<uint8_t>& data, AudioWriteOptions options = {});

private:
  AudioRpcClient& audio_;
  std::string session_id_;
  uint32_t numeric_session_id_ = 0;
  AudioStreamDescriptor stream_;
  uint32_t next_sequence_ = 1;
};

class AudioCapture {
public:
  AudioCapture(AudioRpcClient& audio, std::string session_id, uint32_t numeric_session_id, AudioStreamDescriptor stream);

  const std::string& sessionId() const;
  const AudioStreamDescriptor& stream() const;
  JsonValue start();
  JsonValue stop();
  JsonValue close();
  JsonValue stats();
  AudioReadResult readFrames(size_t max_bytes, AudioReadOptions options = {});

private:
  AudioRpcClient& audio_;
  std::string session_id_;
  uint32_t numeric_session_id_ = 0;
  AudioStreamDescriptor stream_;
  uint32_t next_sequence_ = 1;
};

class AudioRpcClient {
public:
  explicit AudioRpcClient(JsonRpcClient& client, IRpcStreamTransport* stream_transport = nullptr);

  AudioPlayback createPlaybackSession(const AudioSessionConfig& config = {});
  AudioCapture createCaptureSession(const AudioSessionConfig& config = {});
  JsonValue listDevices();
  JsonValue listSessions();
  JsonValue callSessionMethod(const std::string& method, const std::string& session_id);
  AudioWriteResult writePlaybackFrames(const std::string& session_id,
                                       uint32_t numeric_session_id,
                                       const AudioStreamDescriptor& stream,
                                       uint32_t& sequence,
                                       const std::vector<uint8_t>& data,
                                       AudioWriteOptions options);
  AudioReadResult readCaptureFrames(uint32_t numeric_session_id,
                                    const AudioStreamDescriptor& stream,
                                    uint32_t& sequence,
                                    size_t max_bytes,
                                    AudioReadOptions options);
  void closeStreamTransport();

private:
  friend class AudioPlayback;
  friend class AudioCapture;

  JsonValue createSessionParams(const AudioSessionConfig& config) const;
  AudioStreamDescriptor parseStreamDescriptor(const JsonValue& result) const;
  static std::string parseSessionId(const JsonValue& result);
  static uint32_t parseNumericSessionId(const JsonValue& result);

  JsonRpcClient& client_;
  IRpcStreamTransport* stream_transport_ = nullptr;
};

} // namespace audio_studio::rpc
