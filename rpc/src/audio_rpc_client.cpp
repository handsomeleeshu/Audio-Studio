#include "audio_rpc_client.hpp"

#include <algorithm>
#include <utility>

namespace audio_studio::rpc {
namespace {

JsonValue sessionParams(const std::string& session_id) {
  JsonValue params = JsonValue::object();
  params["session_id"] = session_id;
  return params;
}

std::string optionalStringField(const JsonValue& object, const std::string& field, const std::string& fallback = {}) {
  if (!object.isObject() || !object.has(field)) return fallback;
  const JsonValue& value = object.at(field);
  if (!value.isString()) return fallback;
  return value.asString();
}

uint32_t optionalU32Field(const JsonValue& object, const std::string& field, uint32_t fallback = 0) {
  if (!object.isObject() || !object.has(field)) return fallback;
  const JsonValue& value = object.at(field);
  if (!value.isNumber()) return fallback;
  return static_cast<uint32_t>(value.asUInt64());
}

bool optionalBoolField(const JsonValue& object, const std::string& field, bool fallback = false) {
  if (!object.isObject() || !object.has(field)) return fallback;
  const JsonValue& value = object.at(field);
  if (!value.isBool()) return fallback;
  return value.asBool();
}

JsonValue parseFrameJsonPayload(const RpcBinaryFrame& frame) {
  const std::string payload(frame.payload.begin(), frame.payload.end());
  return parseJson(payload);
}

void throwIfErrorFrame(const RpcBinaryFrame& frame) {
  if (frame.header.message_type != RpcMessageType::kError) return;
  JsonValue error = parseFrameJsonPayload(frame);
  const std::string message = optionalStringField(error, "message", "stream error");
  throw JsonRpcError(JsonRpcErrorCode::kInternalError, message);
}

AudioWriteResult parseWriteAck(const RpcBinaryFrame& ack) {
  AudioWriteResult result;
  throwIfErrorFrame(ack);
  if (ack.header.message_type != RpcMessageType::kStreamAck) {
    throw JsonRpcError(JsonRpcErrorCode::kInternalError, "expected StreamAck frame");
  }
  const std::string payload(ack.payload.begin(), ack.payload.end());
  const JsonValue value = parseJson(payload);
  result.accepted = optionalBoolField(value, "accepted", false);
  result.bytes = optionalU32Field(value, "accepted_bytes", 0);
  result.queued_bytes = optionalU32Field(value, "queued_bytes", 0);
  result.credit_bytes = optionalU32Field(value, "credit_bytes", 0);
  return result;
}

AudioWriteResult parseDebugWrite(const JsonValue& value) {
  AudioWriteResult result;
  result.accepted = value.has("accepted") && value.at("accepted").isBool() && value.at("accepted").asBool();
  result.bytes = optionalU32Field(value, "bytes", 0);
  return result;
}

} // namespace

AudioPlayback::AudioPlayback(AudioRpcClient& audio,
                             std::string session_id,
                             uint32_t numeric_session_id,
                             AudioStreamDescriptor stream)
  : audio_(audio),
    session_id_(std::move(session_id)),
    numeric_session_id_(numeric_session_id),
    stream_(std::move(stream)) {}

const std::string& AudioPlayback::sessionId() const {
  return session_id_;
}

const AudioStreamDescriptor& AudioPlayback::stream() const {
  return stream_;
}

JsonValue AudioPlayback::start() {
  return audio_.callSessionMethod("audio.start", session_id_);
}

JsonValue AudioPlayback::drain() {
  return audio_.callSessionMethod("audio.drain", session_id_);
}

JsonValue AudioPlayback::stop() {
  return audio_.callSessionMethod("audio.stop", session_id_);
}

JsonValue AudioPlayback::close() {
  return audio_.callSessionMethod("audio.closeSession", session_id_);
}

JsonValue AudioPlayback::stats() {
  return audio_.callSessionMethod("audio.getStats", session_id_);
}

AudioWriteResult AudioPlayback::writeFrames(const std::vector<uint8_t>& data, AudioWriteOptions options) {
  return audio_.writePlaybackFrames(session_id_, numeric_session_id_, stream_, next_sequence_, data, options);
}

AudioWriteResult AudioPlayback::writeFrames(uint32_t debug_byte_count) {
  return audio_.writePlaybackFramesDebug(session_id_, debug_byte_count);
}

AudioCapture::AudioCapture(AudioRpcClient& audio,
                           std::string session_id,
                           uint32_t numeric_session_id,
                           AudioStreamDescriptor stream)
  : audio_(audio),
    session_id_(std::move(session_id)),
    numeric_session_id_(numeric_session_id),
    stream_(std::move(stream)) {}

const std::string& AudioCapture::sessionId() const {
  return session_id_;
}

const AudioStreamDescriptor& AudioCapture::stream() const {
  return stream_;
}

JsonValue AudioCapture::start() {
  return audio_.callSessionMethod("audio.start", session_id_);
}

JsonValue AudioCapture::stop() {
  return audio_.callSessionMethod("audio.stop", session_id_);
}

JsonValue AudioCapture::close() {
  return audio_.callSessionMethod("audio.closeSession", session_id_);
}

JsonValue AudioCapture::stats() {
  return audio_.callSessionMethod("audio.getStats", session_id_);
}

AudioReadResult AudioCapture::readFrames(size_t max_bytes, AudioReadOptions options) {
  return audio_.readCaptureFrames(numeric_session_id_, stream_, next_sequence_, max_bytes, options);
}

AudioRpcClient::AudioRpcClient(JsonRpcClient& client, IRpcStreamTransport* stream_transport)
  : client_(client), stream_transport_(stream_transport) {}

AudioPlayback AudioRpcClient::createPlaybackSession(const AudioSessionConfig& config) {
  JsonValue result = client_.call("audio.createPlaybackSession", createSessionParams(config));
  return AudioPlayback(*this, parseSessionId(result), parseNumericSessionId(result), parseStreamDescriptor(result));
}

AudioCapture AudioRpcClient::createCaptureSession(const AudioSessionConfig& config) {
  JsonValue result = client_.call("audio.createCaptureSession", createSessionParams(config));
  return AudioCapture(*this, parseSessionId(result), parseNumericSessionId(result), parseStreamDescriptor(result));
}

JsonValue AudioRpcClient::listDevices() {
  return client_.call("audio.listDevices");
}

JsonValue AudioRpcClient::listSessions() {
  return client_.call("audio.listSessions");
}

JsonValue AudioRpcClient::callSessionMethod(const std::string& method, const std::string& session_id) {
  return client_.call(method, sessionParams(session_id));
}

AudioWriteResult AudioRpcClient::writePlaybackFrames(const std::string& session_id,
                                                     uint32_t numeric_session_id,
                                                     const AudioStreamDescriptor& stream,
                                                     uint32_t& sequence,
                                                     const std::vector<uint8_t>& data,
                                                     AudioWriteOptions options) {
  if (data.empty()) return {};
  if (stream_transport_ == nullptr) {
    if (data.size() > 65536) {
      throw JsonRpcError(JsonRpcErrorCode::kInvalidParams,
                         "AudioPlayback::writeFrames requires a binary stream transport for payloads larger than 65536 bytes");
    }
    return writePlaybackFramesDebug(session_id, static_cast<uint32_t>(data.size()));
  }

  AudioWriteResult total;
  const size_t max_chunk = std::max<uint32_t>(1, stream.max_chunk_bytes);
  size_t offset = 0;
  while (offset < data.size()) {
    const size_t chunk = std::min(max_chunk, data.size() - offset);
    RpcBinaryFrame frame;
    frame.header.message_type = RpcMessageType::kStreamData;
    frame.header.service_id = static_cast<uint16_t>(RpcServiceId::kAudio);
    frame.header.method_id = kRpcAudioMethodWriteFrames;
    frame.header.payload_type = RpcPayloadType::kBinary;
    frame.header.request_id = sequence++;
    frame.header.session_id = numeric_session_id;
    frame.header.stream_id = stream.numeric_stream_id;
    frame.header.flags = options.timeout_ms;
    frame.payload.assign(data.begin() + static_cast<long>(offset), data.begin() + static_cast<long>(offset + chunk));

    AudioWriteResult ack = parseWriteAck(stream_transport_->exchange(frame));
    if (!ack.accepted) throw JsonRpcError(JsonRpcErrorCode::kInternalError, "stream write was not accepted");
    total.accepted = true;
    total.bytes += ack.bytes;
    total.queued_bytes = ack.queued_bytes;
    total.credit_bytes = ack.credit_bytes;
    offset += chunk;
  }
  return total;
}

AudioWriteResult AudioRpcClient::writePlaybackFramesDebug(const std::string& session_id, uint32_t bytes) {
  JsonValue params = sessionParams(session_id);
  params["bytes"] = bytes;
  return parseDebugWrite(client_.call("audio.writeFrames", params));
}

AudioReadResult AudioRpcClient::readCaptureFrames(uint32_t numeric_session_id,
                                                  const AudioStreamDescriptor& stream,
                                                  uint32_t& sequence,
                                                  size_t max_bytes,
                                                  AudioReadOptions options) {
  if (stream_transport_ == nullptr) {
    throw JsonRpcError(JsonRpcErrorCode::kInvalidParams, "AudioCapture::readFrames requires a binary stream transport");
  }
  if (max_bytes == 0) return {};

  JsonValue request = JsonValue::object();
  request["max_bytes"] = static_cast<uint32_t>(max_bytes);
  const std::string request_json = request.dump();

  RpcBinaryFrame frame;
  frame.header.message_type = RpcMessageType::kRequest;
  frame.header.service_id = static_cast<uint16_t>(RpcServiceId::kAudio);
  frame.header.method_id = kRpcAudioMethodReadFrames;
  frame.header.payload_type = RpcPayloadType::kJson;
  frame.header.request_id = sequence++;
  frame.header.session_id = numeric_session_id;
  frame.header.stream_id = stream.numeric_stream_id;
  frame.header.flags = options.timeout_ms;
  frame.payload.assign(request_json.begin(), request_json.end());

  RpcBinaryFrame response = stream_transport_->exchange(frame);
  throwIfErrorFrame(response);
  if (response.header.message_type != RpcMessageType::kStreamData) {
    throw JsonRpcError(JsonRpcErrorCode::kInternalError, "expected StreamData frame");
  }

  AudioReadResult result;
  result.ok = true;
  result.bytes = response.payload.size();
  result.data = std::move(response.payload);
  return result;
}

JsonValue AudioRpcClient::createSessionParams(const AudioSessionConfig& config) const {
  JsonValue params = JsonValue::object();
  if (!config.session_id.empty()) params["session_id"] = config.session_id;
  params["sample_rate"] = config.sample_rate;
  params["channels"] = static_cast<uint32_t>(config.channels);
  params["bytes_per_sample"] = static_cast<uint32_t>(config.bytes_per_sample);
  if (!config.sample_format.empty()) params["sample_format"] = config.sample_format;
  if (!config.device_name.empty()) params["device"] = config.device_name;
  if (!config.driver_factory.empty()) params["driver_factory"] = config.driver_factory;
  return params;
}

AudioStreamDescriptor AudioRpcClient::parseStreamDescriptor(const JsonValue& result) const {
  if (!result.isObject() || !result.has("stream") || !result.at("stream").isObject()) {
    throw JsonRpcError(JsonRpcErrorCode::kInternalError, "audio session result missing stream descriptor");
  }
  const JsonValue& value = result.at("stream");
  AudioStreamDescriptor stream;
  stream.stream_id = optionalStringField(value, "stream_id");
  stream.numeric_stream_id = optionalU32Field(value, "numeric_stream_id", 0);
  stream.uri = optionalStringField(value, "uri");
  stream.direction = optionalStringField(value, "direction");
  stream.framing = optionalStringField(value, "framing");
  stream.payload = optionalStringField(value, "payload");
  stream.max_chunk_bytes = optionalU32Field(value, "max_chunk_bytes", 65536);
  stream.default_timeout_ms = optionalU32Field(value, "default_timeout_ms", 5000);
  stream.blocking = optionalBoolField(value, "blocking", true);
  return stream;
}

std::string AudioRpcClient::parseSessionId(const JsonValue& result) {
  if (!result.isObject() || !result.has("session")) {
    throw JsonRpcError(JsonRpcErrorCode::kInternalError, "audio session result missing session object");
  }
  return requireStringParam(result.at("session"), "session_id");
}

uint32_t AudioRpcClient::parseNumericSessionId(const JsonValue& result) {
  if (!result.isObject() || !result.has("session") || !result.at("session").isObject()) {
    throw JsonRpcError(JsonRpcErrorCode::kInternalError, "audio session result missing session object");
  }
  return optionalU32Field(result.at("session"), "numeric_session_id", 0);
}

} // namespace audio_studio::rpc
