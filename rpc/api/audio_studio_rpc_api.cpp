#include "rpc_api_registry.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "autoconfig.h"
#include "audio_service.hpp"
#include "rpc_runtime_context.hpp"

namespace audio_studio::rpc {
namespace {

const char* toolOs() {
#if defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#else
  return "linux";
#endif
}

const char* targetPlatform() {
#if defined(CONFIG_TARGET_PLATFORM_SIMULATOR)
  return "simulator";
#else
  return "a2";
#endif
}

std::string defaultAudioDriverFactory() {
#if defined(_WIN32)
  return "wasapi";
#else
  return "alsa";
#endif
}

JsonValue statusResult(const framework::Status& status) {
  if (!status.ok()) throw JsonRpcError(JsonRpcErrorCode::kInvalidParams, status.message());
  JsonValue result = JsonValue::object();
  result["ok"] = true;
  result["code"] = status.codeString();
  result["message"] = status.message();
  return result;
}

std::string optionalStringParam(const JsonValue& params, const std::string& name, const std::string& fallback) {
  if (!params.isObject() || !params.has(name)) return fallback;
  const JsonValue& value = params.at(name);
  if (!value.isString()) throw JsonRpcError(JsonRpcErrorCode::kInvalidParams, "param must be string: " + name);
  return value.asString();
}

std::string sampleFormatFromBytes(uint16_t bytes_per_sample) {
  switch (bytes_per_sample) {
    case 1:
      return "u8";
    case 2:
      return "s16le";
    case 3:
      return "s24_3le";
    case 4:
      return "s32le";
    default:
      return "unknown";
  }
}

JsonValue formatJson(const framework::audio::AudioStream& stream) {
  JsonValue format = JsonValue::object();
  format["sample_rate"] = static_cast<uint32_t>(stream.sample_rate);
  format["channels"] = static_cast<uint32_t>(stream.channels);
  format["bytes_per_sample"] = static_cast<uint32_t>(stream.bytes_per_sample);
  format["sample_format"] = sampleFormatFromBytes(static_cast<uint16_t>(stream.bytes_per_sample));
  return format;
}

JsonValue streamToJson(const framework::audio::AudioStream& stream, RpcRuntimeContext& context) {
  JsonValue value = JsonValue::object();
  value["session_id"] = stream.id;
  value["numeric_session_id"] = context.numericSessionId(stream.id);
  value["direction"] = stream.direction == framework::audio::StreamDirection::kPlayback ? "playback" : "capture";
  value["driver_factory"] = stream.driver_factory;
  value["device_name"] = stream.device_name;
  value["sample_rate"] = static_cast<uint32_t>(stream.sample_rate);
  value["channels"] = static_cast<uint32_t>(stream.channels);
  value["bytes_per_sample"] = static_cast<uint32_t>(stream.bytes_per_sample);
  value["sample_format"] = sampleFormatFromBytes(static_cast<uint16_t>(stream.bytes_per_sample));
  value["blocking_write"] = stream.blocking_write;
  value["prepared"] = stream.prepared;
  value["running"] = stream.running;
  return value;
}

JsonValue streamDescriptorJson(const framework::audio::AudioStream& stream, RpcRuntimeContext& context) {
  const auto& defaults = context.streamDefaults();
  const uint32_t numeric_stream_id = context.numericStreamId(stream.id);
  const std::string stream_id = "stream_" + stream.id;
  const std::string base_uri = defaults.stream_uri_base.empty()
                                 ? defaults.socket_scheme + "://" + defaults.host + ":" + std::to_string(defaults.port)
                                 : defaults.stream_uri_base;
  const std::string uri = base_uri + "/streams/" + stream_id;

  JsonValue descriptor = JsonValue::object();
  descriptor["stream_id"] = stream_id;
  descriptor["numeric_stream_id"] = numeric_stream_id;
  descriptor["uri"] = uri;
  descriptor["direction"] = stream.direction == framework::audio::StreamDirection::kPlayback ? "write" : "read";
  descriptor["framing"] = "asrp-v1";
  descriptor["payload"] = "audio/pcm";
  descriptor["max_chunk_bytes"] = defaults.max_chunk_bytes;
  descriptor["default_timeout_ms"] = defaults.timeout_ms;
  descriptor["blocking"] = stream.blocking_write;
  descriptor["token_usage"] = "first_frame";
  return descriptor;
}

framework::audio::AudioStream audioStreamFromParams(RpcRuntimeContext& context,
                                                    const JsonValue& params,
                                                    framework::audio::StreamDirection direction) {
  JsonValue object = requireObjectParams(params, "audio.createSession");
  const std::string prefix = direction == framework::audio::StreamDirection::kPlayback ? "playback" : "capture";

  framework::audio::AudioStream stream;
  stream.id = optionalStringParam(object, "session_id", context.nextSessionId(prefix));
  stream.direction = direction;
  stream.driver_factory = optionalStringParam(object, "driver_factory", defaultAudioDriverFactory());
  stream.device_name = optionalStringParam(object, "device_name", optionalStringParam(object, "device", "default"));
  stream.sample_rate = static_cast<int>(optionalUInt32Param(object, "sample_rate", 48000));
  stream.channels = static_cast<int>(optionalUInt16Param(object, "channels", 2));
  stream.bytes_per_sample = static_cast<int>(optionalUInt16Param(object, "bytes_per_sample", 2));
  stream.blocking_write = optionalBoolParam(object, "blocking_write", true);
  return stream;
}

JsonValue createAudioSession(RpcRuntimeContext& context,
                             const JsonValue& params,
                             framework::audio::StreamDirection direction) {
  auto stream = audioStreamFromParams(context, params, direction);
  const std::string id = stream.id;
  if (direction == framework::audio::StreamDirection::kPlayback) {
    std::shared_ptr<framework::audio::AudioPlaybackSession> session;
    (void)statusResult(context.audio().createPlaybackSession(std::move(stream), session));
  } else {
    std::shared_ptr<framework::audio::AudioCaptureSession> session;
    (void)statusResult(context.audio().createCaptureSession(std::move(stream), session));
  }

  framework::audio::AudioStream created;
  (void)statusResult(context.audio().get(id, created));

  JsonValue result = JsonValue::object();
  result["session"] = streamToJson(created, context);
  result["format"] = formatJson(created);
  result["stream"] = streamDescriptorJson(created, context);
  return result;
}

std::string sessionId(const JsonValue& params) {
  return requireStringParam(params, "session_id");
}

JsonValue sessionResult(RpcRuntimeContext& context, const std::string& id) {
  framework::audio::AudioStream stream;
  (void)statusResult(context.audio().get(id, stream));
  JsonValue result = JsonValue::object();
  result["session"] = streamToJson(stream, context);
  return result;
}

JsonValue health(RpcRuntimeContext&, const JsonValue&) {
  JsonValue result = JsonValue::object();
  result["ok"] = true;
  result["tool_os"] = toolOs();
  result["platform"] = targetPlatform();
  result["rpc"] = "json-rpc-2.0";
  result["default_transport"] = "socket";
  return result;
}

JsonValue listMethods(RpcRuntimeContext&, const JsonValue&) {
  return audioStudioRpcApiRegistry().listMethods();
}

JsonValue describe(RpcRuntimeContext&, const JsonValue& params) {
  const std::string method = requireStringParam(params, "method");
  return audioStudioRpcApiRegistry().describeMethod(method);
}

JsonValue createPlayback(RpcRuntimeContext& context, const JsonValue& params) {
  return createAudioSession(context, params, framework::audio::StreamDirection::kPlayback);
}

JsonValue createCapture(RpcRuntimeContext& context, const JsonValue& params) {
  return createAudioSession(context, params, framework::audio::StreamDirection::kCapture);
}

JsonValue prepare(RpcRuntimeContext& context, const JsonValue& params) {
  const std::string id = sessionId(params);
  (void)statusResult(context.audio().prepare(id));
  return sessionResult(context, id);
}

JsonValue start(RpcRuntimeContext& context, const JsonValue& params) {
  const std::string id = sessionId(params);
  (void)statusResult(context.audio().start(id));
  return sessionResult(context, id);
}

JsonValue drain(RpcRuntimeContext& context, const JsonValue& params) {
  const std::string id = sessionId(params);
  (void)statusResult(context.audio().drain(id));
  return sessionResult(context, id);
}

JsonValue stop(RpcRuntimeContext& context, const JsonValue& params) {
  const std::string id = sessionId(params);
  (void)statusResult(context.audio().stop(id));
  return sessionResult(context, id);
}

JsonValue closeSession(RpcRuntimeContext& context, const JsonValue& params) {
  const std::string id = sessionId(params);
  (void)statusResult(context.audio().remove(id));
  context.releaseSession(id);
  JsonValue result = JsonValue::object();
  result["session_id"] = id;
  result["closed"] = true;
  return result;
}

JsonValue listSessions(RpcRuntimeContext& context, const JsonValue&) {
  JsonValue sessions = JsonValue::array();
  for (const auto& stream : context.audio().list()) sessions.pushBack(streamToJson(stream, context));
  JsonValue result = JsonValue::object();
  result["sessions"] = std::move(sessions);
  return result;
}

JsonValue getStats(RpcRuntimeContext& context, const JsonValue& params) {
  const std::string id = sessionId(params);
  framework::audio::AudioStream stream;
  (void)statusResult(context.audio().get(id, stream));
  framework::audio::AudioIoStats stats;
  (void)statusResult(context.audio().getStats(id, stats));
  JsonValue result = JsonValue::object();
  result["session_id"] = id;
  result["running"] = stream.running;
  result["prepared"] = stream.prepared;
  result["frames_written"] = static_cast<uint32_t>(stats.frames_written);
  result["frames_read"] = static_cast<uint32_t>(stats.frames_read);
  return result;
}

JsonValue listDevices(RpcRuntimeContext&, const JsonValue&) {
  JsonValue devices = JsonValue::array();
  JsonValue host = JsonValue::object();
  host["name"] = "default";
  host["driver"] = defaultAudioDriverFactory();
  host["playback"] = true;
  host["capture"] = true;
  devices.pushBack(std::move(host));

  JsonValue result = JsonValue::object();
  result["devices"] = std::move(devices);
  return result;
}

JsonValue audioParamsExample() {
  JsonValue params = JsonValue::object();
  params["sample_rate"] = 48000;
  params["channels"] = 2;
  params["bytes_per_sample"] = 2;
  params["device"] = "default";
  params["driver_factory"] = defaultAudioDriverFactory();
  params["blocking_write"] = true;
  return params;
}

JsonValue sessionParamsExample() {
  JsonValue params = JsonValue::object();
  params["session_id"] = "playback_1";
  return params;
}

} // namespace

RpcApiRegistry& audioStudioRpcApiRegistry() {
  static RpcApiRegistry registry = [] {
    RpcApiRegistry api;

    api.addMethod({"server.health", "Get Audio Studio server health", "server", "1.0",
                   JsonValue::object(), JsonValue::object(),
                   {{"as_control", "as_log", "as_dump"}, "get-health"},
                   {true, JsonValue::object()}, health});

    api.addMethod({"rpc.listMethods", "List registered RPC methods", "rpc", "1.0",
                   JsonValue::object(), JsonValue::object(),
                   {{}, ""}, {true, JsonValue::object()}, listMethods});

    JsonValue describe_params = JsonValue::object();
    describe_params["method"] = "server.health";
    api.addMethod({"rpc.describe", "Describe one registered RPC method", "rpc", "1.0",
                   describe_params, JsonValue::object(),
                   {{}, ""}, {true, describe_params}, describe});

    api.addMethod({"audio.listDevices", "List available audio devices", "audio", "1.0",
                   JsonValue::object(), JsonValue::object(),
                   {{}, ""}, {true, JsonValue::object()}, listDevices});

    api.addMethod({"audio.createPlaybackSession", "Create an audio playback session", "audio", "1.0",
                   audioParamsExample(), JsonValue::object(),
                   {{"as_play"}, "play"}, {true, audioParamsExample()}, createPlayback});

    api.addMethod({"audio.createCaptureSession", "Create an audio capture session", "audio", "1.0",
                   audioParamsExample(), JsonValue::object(),
                   {{"as_record"}, "record"}, {true, audioParamsExample()}, createCapture});

    api.addMethod({"audio.prepare", "Prepare an audio session", "audio", "1.0",
                   sessionParamsExample(), JsonValue::object(),
                   {{}, ""}, {false, JsonValue::object()}, prepare});

    api.addMethod({"audio.start", "Start an audio session", "audio", "1.0",
                   sessionParamsExample(), JsonValue::object(),
                   {{}, ""}, {false, JsonValue::object()}, start});

    api.addMethod({"audio.drain", "Drain a playback audio session", "audio", "1.0",
                   sessionParamsExample(), JsonValue::object(),
                   {{}, ""}, {false, JsonValue::object()}, drain});

    api.addMethod({"audio.stop", "Stop an audio session", "audio", "1.0",
                   sessionParamsExample(), JsonValue::object(),
                   {{}, ""}, {false, JsonValue::object()}, stop});

    api.addMethod({"audio.closeSession", "Close an audio session", "audio", "1.0",
                   sessionParamsExample(), JsonValue::object(),
                   {{}, ""}, {false, JsonValue::object()}, closeSession});

    api.addMethod({"audio.listSessions", "List audio sessions", "audio", "1.0",
                   JsonValue::object(), JsonValue::object(),
                   {{}, ""}, {true, JsonValue::object()}, listSessions});

    api.addMethod({"audio.getStats", "Get audio session stats", "audio", "1.0",
                   sessionParamsExample(), JsonValue::object(),
                   {{}, ""}, {false, JsonValue::object()}, getStats});

    return api;
  }();
  return registry;
}

void registerAudioStudioRpcMethods(JsonRpcEndpoint& endpoint, RpcRuntimeContextPtr context) {
  audioStudioRpcApiRegistry().registerEndpoint(endpoint, std::move(context));
}

} // namespace audio_studio::rpc
