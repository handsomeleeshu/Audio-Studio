#include "audio_studio/rpc/audio_rpc.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace audio_studio::rpc {
namespace {

JsonValue statusResult(const framework::Status& status) {
  JsonValue result = JsonValue::object();
  result["ok"] = status.ok();
  result["code"] = status.codeString();
  result["message"] = status.message();
  if (!status.ok()) {
    throw JsonRpcError(JsonRpcErrorCode::kInvalidParams, status.message());
  }
  return result;
}

JsonValue streamToJson(const framework::audio::AudioStream& stream) {
  JsonValue value = JsonValue::object();
  value["session_id"] = stream.id;
  value["direction"] = stream.direction == framework::audio::StreamDirection::kPlayback ? "playback" : "capture";
  value["sample_rate"] = stream.sample_rate;
  value["channels"] = stream.channels;
  value["bytes_per_sample"] = stream.bytes_per_sample;
  value["prepared"] = stream.prepared;
  value["running"] = stream.running;
  return value;
}

std::string sessionId(const JsonValue& params) {
  return requireStringParam(params, "session_id");
}

framework::audio::AudioStream playbackStreamFromParams(const JsonValue& params) {
  JsonValue object = requireObjectParams(params, "audio.createPlaybackSession");
  framework::audio::AudioStream stream;
  stream.id = requireStringParam(object, "session_id");
  stream.direction = framework::audio::StreamDirection::kPlayback;
  stream.sample_rate = static_cast<int>(optionalUInt32Param(object, "sample_rate", 48000));
  stream.channels = static_cast<int>(optionalUInt16Param(object, "channels", 2));
  stream.bytes_per_sample = static_cast<int>(optionalUInt16Param(object, "bytes_per_sample", 2));
  return stream;
}

} // namespace

void registerAudioRpcMethods(JsonRpcEndpoint& endpoint, framework::audio::AudioService& audio_service) {
  endpoint.addMethod("audio.createPlaybackSession", [&audio_service](const JsonValue& params) {
    auto stream = playbackStreamFromParams(params);
    const std::string id = stream.id;
    (void)statusResult(audio_service.create(std::move(stream)));
    framework::audio::AudioStream created;
    (void)audio_service.get(id, created);
    JsonValue result = JsonValue::object();
    result["session"] = streamToJson(created);
    return result;
  });

  endpoint.addMethod("audio.prepare", [&audio_service](const JsonValue& params) {
    const std::string id = sessionId(params);
    (void)statusResult(audio_service.prepare(id));
    framework::audio::AudioStream stream;
    (void)audio_service.get(id, stream);
    JsonValue result = JsonValue::object();
    result["session"] = streamToJson(stream);
    return result;
  });

  endpoint.addMethod("audio.start", [&audio_service](const JsonValue& params) {
    const std::string id = sessionId(params);
    (void)statusResult(audio_service.start(id));
    framework::audio::AudioStream stream;
    (void)audio_service.get(id, stream);
    JsonValue result = JsonValue::object();
    result["session"] = streamToJson(stream);
    return result;
  });

  endpoint.addMethod("audio.writeFrames", [&audio_service](const JsonValue& params) {
    const std::string id = sessionId(params);
    framework::audio::AudioStream stream;
    auto status = audio_service.get(id, stream);
    (void)statusResult(status);
    if (!stream.running) {
      throw JsonRpcError(JsonRpcErrorCode::kInvalidParams, "audio session is not running: " + id);
    }
    JsonValue result = JsonValue::object();
    result["session_id"] = id;
    result["accepted"] = true;
    result["bytes"] = optionalUInt32Param(params, "bytes", 0);
    return result;
  });

  endpoint.addMethod("audio.stop", [&audio_service](const JsonValue& params) {
    const std::string id = sessionId(params);
    (void)statusResult(audio_service.stop(id));
    framework::audio::AudioStream stream;
    (void)audio_service.get(id, stream);
    JsonValue result = JsonValue::object();
    result["session"] = streamToJson(stream);
    return result;
  });

  endpoint.addMethod("audio.closeSession", [&audio_service](const JsonValue& params) {
    const std::string id = sessionId(params);
    (void)statusResult(audio_service.remove(id));
    JsonValue result = JsonValue::object();
    result["session_id"] = id;
    result["closed"] = true;
    return result;
  });

  endpoint.addMethod("audio.listSessions", [&audio_service](const JsonValue&) {
    JsonValue sessions = JsonValue::array();
    for (const auto& stream : audio_service.list()) sessions.pushBack(streamToJson(stream));
    JsonValue result = JsonValue::object();
    result["sessions"] = std::move(sessions);
    return result;
  });
}

} // namespace audio_studio::rpc
