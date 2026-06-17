#include <cassert>
#include <chrono>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

#include "autoconfig.h"
#include "audio_service.hpp"
#include "service_registry.hpp"
#include "status.hpp"
#include "audio_rpc.hpp"
#include "audio_rpc_client.hpp"
#include "json_rpc.hpp"
#include "json_value.hpp"
#include "rpc_framing.hpp"
#include "rpc_pipe_transport.hpp"
#include "rpc_server.hpp"
#include "rpc_socket_transport.hpp"
#include "driver_manager.hpp"
#if defined(CONFIG_FRAMEWORK_CONFIG)
#include "config_service.hpp"
#endif

namespace {

uint16_t findFreePort(audio_studio::drivers::socket::ISocketDriver& driver) {
  for (uint16_t port = 29170; port < 29220; ++port) {
    auto socket = driver.createSocket(audio_studio::drivers::socket::SocketType::Tcp);
    if (!socket) continue;
    if (!socket->open({audio_studio::drivers::socket::SocketType::Tcp}).ok()) continue;
    auto status = socket->bind({"127.0.0.1", port});
    socket->close();
    if (status.ok()) return port;
  }
  assert(false && "no free test port");
  return 0;
}

void throwIfThreadFailed(const std::exception_ptr& error) {
  if (error) std::rethrow_exception(error);
}

audio_studio::rpc::AudioSessionConfig makePlaybackConfig(const std::string& id) {
  audio_studio::rpc::AudioSessionConfig config;
  config.session_id = id;
  config.sample_rate = 48000;
  config.channels = 2;
  config.bytes_per_sample = 2;
  return config;
}

audio_studio::rpc::AudioSessionConfig makeCaptureConfig(const std::string& id) {
  auto config = makePlaybackConfig(id);
  return config;
}

audio_studio::rpc::RpcBinaryFrame streamError(const audio_studio::rpc::RpcBinaryFrame& request, const std::string& message) {
  audio_studio::rpc::JsonValue payload = audio_studio::rpc::JsonValue::object();
  payload["ok"] = false;
  payload["message"] = message;
  const std::string json = payload.dump();

  audio_studio::rpc::RpcBinaryFrame response;
  response.header.message_type = audio_studio::rpc::RpcMessageType::kError;
  response.header.service_id = request.header.service_id;
  response.header.method_id = request.header.method_id;
  response.header.payload_type = audio_studio::rpc::RpcPayloadType::kJson;
  response.header.request_id = request.header.request_id;
  response.header.session_id = request.header.session_id;
  response.header.stream_id = request.header.stream_id;
  response.payload.assign(json.begin(), json.end());
  return response;
}

audio_studio::rpc::RpcBinaryFrame audioServiceStreamHandler(audio_studio::rpc::RpcRuntimeContext& context,
                                                           const audio_studio::rpc::RpcBinaryFrame& request) {
  using namespace audio_studio;
  if (request.header.service_id != static_cast<uint16_t>(rpc::RpcServiceId::kAudio)) {
    return rpc::makeDefaultStreamAck(request);
  }

  std::string session_id;
  if (!context.sessionIdForNumeric(request.header.session_id, session_id)) {
    return streamError(request, "audio stream session not found for numeric session: " + std::to_string(request.header.session_id));
  }

  if (request.header.method_id == rpc::kRpcAudioMethodWriteFrames) {
    size_t accepted = 0;
    auto status = context.audio().writeFrames(session_id, request.payload, request.header.flags, accepted);
    if (!status.ok()) return streamError(request, status.message());

    rpc::JsonValue payload = rpc::JsonValue::object();
    payload["accepted"] = true;
    payload["accepted_bytes"] = static_cast<uint32_t>(accepted);
    payload["queued_bytes"] = static_cast<uint32_t>(0);
    payload["credit_bytes"] = static_cast<uint32_t>(65536);
    const std::string json = payload.dump();

    rpc::RpcBinaryFrame response;
    response.header.message_type = rpc::RpcMessageType::kStreamAck;
    response.header.service_id = request.header.service_id;
    response.header.method_id = request.header.method_id;
    response.header.payload_type = rpc::RpcPayloadType::kJson;
    response.header.request_id = request.header.request_id;
    response.header.session_id = request.header.session_id;
    response.header.stream_id = request.header.stream_id;
    response.payload.assign(json.begin(), json.end());
    return response;
  }

  if (request.header.method_id == rpc::kRpcAudioMethodReadFrames) {
    uint32_t max_bytes = 4096;
    if (!request.payload.empty()) {
      const std::string json(request.payload.begin(), request.payload.end());
      max_bytes = rpc::optionalUInt32Param(rpc::parseJson(json), "max_bytes", max_bytes);
    }

    std::vector<uint8_t> data;
    auto status = context.audio().readFrames(session_id, max_bytes, request.header.flags, data);
    if (!status.ok()) return streamError(request, status.message());

    rpc::RpcBinaryFrame response;
    response.header.message_type = rpc::RpcMessageType::kStreamData;
    response.header.service_id = request.header.service_id;
    response.header.method_id = request.header.method_id;
    response.header.payload_type = rpc::RpcPayloadType::kBinary;
    response.header.request_id = request.header.request_id;
    response.header.session_id = request.header.session_id;
    response.header.stream_id = request.header.stream_id;
    response.payload = std::move(data);
    return response;
  }

  return streamError(request, "unsupported stream method");
}

std::string readFileText(const std::string& path) {
  std::ifstream input(path);
  std::ostringstream out;
  out << input.rdbuf();
  return out.str();
}

} // namespace

int main() {
  audio_studio::framework::Status ok = audio_studio::framework::Status::success();
  assert(ok.ok());
  assert(ok.codeString() == "OK");
  assert(ok.toJson().find("\"ok\":true") != std::string::npos);

  audio_studio::framework::ServiceRegistry registry;
  assert(registry.registerService("health", "server health service").ok());
  assert(registry.hasService("health"));
  assert(registry.describe("health") == "server health service");
  assert(!registry.registerService("", "bad").ok());
  assert(!registry.registerService("health", "duplicate").ok());

  using audio_studio::rpc::InProcessJsonRpcTransport;
  using audio_studio::rpc::JsonRpcClient;
  using audio_studio::rpc::JsonRpcEndpoint;
  using audio_studio::rpc::JsonRpcError;
  using audio_studio::rpc::JsonRpcErrorCode;
  using audio_studio::rpc::JsonValue;
  using audio_studio::rpc::parseJson;

  JsonValue parsed = parseJson(R"({"method":"server.health","params":{"target":"a2"}})");
  assert(parsed.isObject());
  assert(parsed.at("method").asString() == "server.health");
  assert(parsed.at("params").at("target").asString() == "a2");
  assert(parseJson(parsed.dump()).at("params").at("target").asString() == "a2");

  JsonRpcEndpoint endpoint;
  assert(endpoint.addMethod("server.health", [](const JsonValue&) {
    JsonValue result = JsonValue::object();
    result["ok"] = true;
    result["service"] = "server";
    return result;
  }));
  assert(!endpoint.addMethod("server.health", [](const JsonValue&) { return JsonValue(); }));
  assert(!endpoint.addMethod("rpc.reserved", [](const JsonValue&) { return JsonValue(); }));

  InProcessJsonRpcTransport transport(endpoint);
  JsonRpcClient client(transport);
  JsonValue health = client.call("server.health");
  assert(health.at("ok").asBool());
  assert(health.at("service").asString() == "server");

  bool saw_method_not_found = false;
  try {
    (void)client.call("server.missing");
  } catch (const JsonRpcError& error) {
    saw_method_not_found = error.code() == JsonRpcErrorCode::kMethodNotFound;
  }
  assert(saw_method_not_found);

  JsonValue parse_error = parseJson(endpoint.handleRequest("{not-json"));
  assert(parse_error.at("error").at("code").asInt64() == static_cast<int>(JsonRpcErrorCode::kParseError));

  const std::string batch_response = endpoint.handleRequest(
    R"([{"jsonrpc":"2.0","id":7,"method":"server.health"},{"jsonrpc":"2.0","method":"server.health"}])");
  JsonValue batch = parseJson(batch_response);
  assert(batch.isArray());
  assert(batch.asArray().size() == 1);
  assert(batch.asArray()[0].at("id").asInt64() == 7);

  audio_studio::framework::audio::AudioService audio_service;
  JsonRpcEndpoint audio_endpoint;
  audio_studio::rpc::registerAudioRpcMethods(audio_endpoint, audio_service);
  InProcessJsonRpcTransport audio_transport(audio_endpoint);
  JsonRpcClient audio_client(audio_transport);
  audio_studio::rpc::AudioRpcClient typed_audio(audio_client);

  auto playback_config = makePlaybackConfig("playback-1");
  playback_config.blocking_write = false;
  auto playback = typed_audio.createPlaybackSession(playback_config);
  assert(playback.sessionId() == "playback-1");
  assert(playback.stream().framing == "asrp-v1");
  assert(playback.stream().direction == "write");
  assert(!playback.stream().blocking);
  assert(playback.stream().uri.find("/streams/stream_playback-1") != std::string::npos);

  JsonValue started = playback.start();
  assert(started.at("session").at("prepared").asBool());
  assert(started.at("session").at("running").asBool());

  JsonValue drained = playback.drain();
  assert(drained.at("session").at("session_id").asString() == "playback-1");

  JsonValue listed = audio_client.call("audio.listSessions");
  assert(listed.at("sessions").asArray().size() == 1);

  JsonValue stopped = playback.stop();
  assert(!stopped.at("session").at("running").asBool());

  JsonValue closed = playback.close();
  assert(closed.at("closed").asBool());
  assert(audio_client.call("audio.listSessions").at("sessions").asArray().empty());

  {
    audio_studio::framework::audio::AudioService multi_audio_service;
    audio_studio::framework::audio::AudioStream first;
    first.id = "multi-playback-1";
    first.direction = audio_studio::framework::audio::StreamDirection::kPlayback;
    audio_studio::framework::audio::AudioStream second = first;
    second.id = "multi-playback-2";
    std::shared_ptr<audio_studio::framework::audio::AudioPlaybackSession> first_session;
    std::shared_ptr<audio_studio::framework::audio::AudioPlaybackSession> second_session;
    assert(multi_audio_service.createPlaybackSession(first, first_session).ok());
    assert(multi_audio_service.createPlaybackSession(second, second_session).ok());
    assert(first_session);
    assert(second_session);
    assert(first_session->start().ok());
    assert(second_session->start().ok());

    size_t first_accepted = 0;
    size_t second_accepted = 0;
    std::thread first_writer([&] {
      assert(first_session->writeFrames(std::vector<uint8_t>(4096, 0x11), 100, first_accepted).ok());
    });
    std::thread second_writer([&] {
      assert(second_session->writeFrames(std::vector<uint8_t>(8192, 0x22), 100, second_accepted).ok());
    });
    first_writer.join();
    second_writer.join();
    assert(first_accepted == 4096);
    assert(second_accepted == 8192);
    assert(first_session->close().ok());
    assert(second_session->close().ok());
  }

  {
    audio_studio::rpc::RpcBinaryFrame frame;
    frame.header.message_type = audio_studio::rpc::RpcMessageType::kStreamData;
    frame.header.service_id = static_cast<uint16_t>(audio_studio::rpc::RpcServiceId::kAudio);
    frame.header.request_id = 77;
    frame.header.session_id = 88;
    frame.header.stream_id = 99;
    frame.payload = {1, 2, 3, 4, 5};
    const auto encoded = audio_studio::rpc::encodeBinaryFrame(frame);
    auto decoded = audio_studio::rpc::decodeBinaryFrame(encoded.data(), encoded.size());
    assert(decoded.header.message_type == audio_studio::rpc::RpcMessageType::kStreamData);
    assert(decoded.header.request_id == 77);
    assert(decoded.payload == frame.payload);
    auto ack = audio_studio::rpc::makeDefaultStreamAck(decoded);
    assert(ack.header.message_type == audio_studio::rpc::RpcMessageType::kStreamAck);
    assert(ack.header.stream_id == 99);
  }

  auto& drivers = audio_studio::drivers::DriverManager::instance();
  assert(drivers.initialize().ok());

#if defined(CONFIG_FRAMEWORK_CONFIG)
  {
    audio_studio::framework::config::ConfigService config_service(&drivers.filesystem(), &drivers.os(), &drivers.dynlib());
    audio_studio::framework::config::ConfigCompileRequest request;
    request.input_path = std::string(AUDIO_STUDIO_TEST_ROOT) + "/config/A2.json";
    request.output_dir = drivers.filesystem().joinPath({
      drivers.os().system().temporaryDirectory(),
      "audio-studio-config-test-" + std::to_string(static_cast<long long>(getpid())),
    });
    (void)drivers.filesystem().remove(request.output_dir);
    request.project_name = "a2_test";
    request.build_tplg = true;
    request.plugin_paths.push_back(AUDIO_STUDIO_CONFIG_TEST_PLUGIN_PATH);

    audio_studio::framework::config::ConfigCompileOutput output;
    assert(config_service.compile(request, output).ok());
    assert(output.ok);
    assert(output.module_type_count == 26);
    assert(output.module_instance_count == 19);
    assert(output.pipeline_count == 4);
    assert(output.runtime_control_count == 29);
    assert(output.install_param_count == 9);
    assert(output.preset_count == 3);
    assert(output.plugin_count == 1);

    audio_studio::drivers::filesystem::FileInfo info;
    assert(drivers.filesystem().stat(output.conf_path, info).ok() && info.size > 0);
    assert(drivers.filesystem().stat(output.tplg_path, info).ok() && info.size > 0);
    assert(drivers.filesystem().stat(output.private_bin_path, info).ok() && info.size > 0);
    assert(drivers.filesystem().stat(output.tplg_decode_conf_path, info).ok() && info.size > 0);

    const std::string ids = readFileText(output.ids_header_path);
    assert(ids.find("AS_MODULE_TYPE_RATE_ASRC") != std::string::npos);
    assert(ids.find("AS_MODULE_TYPE_SERVICE_ASRC") == std::string::npos);
    assert(ids.find("#define AS_PARAM_GAIN_VOLUME_VOL_DB 0x172E41DCu") != std::string::npos);
    assert(ids.find("#define AS_CONTROL_PLAY_MAIN_VOL_VOL_DB 0xCD13BD21u") != std::string::npos);
    const std::string presets = readFileText(output.preset_header_path);
    assert(presets.find("AS_PRESET_PLAYBACK_MUSIC") != std::string::npos);
    const std::string private_payload = readFileText(output.private_bin_path);
    assert(private_payload.find("as-generic-runtime-json-v1") != std::string::npos);
    assert(private_payload.find("as-generic-install-json-v1") != std::string::npos);
    assert(private_payload.find("as-generic-preset-json-v1") != std::string::npos);
    assert(private_payload.find("\"pipelines\"") != std::string::npos);
    assert(private_payload.find("\"dai_id\":\"CODEC_OUT_DAI0\"") != std::string::npos);
    assert(private_payload.find("\"tdm_slots\":8") != std::string::npos);
    assert(private_payload.find("\"codec_format\"") == std::string::npos);
    assert(private_payload.find("\"config_format\"") != std::string::npos);
    const std::string alsatplg_log = readFileText(output.alsatplg_log_path);
    const std::string decode_log = readFileText(output.tplg_decode_log_path);
    assert(alsatplg_log.find("ALSA lib") == std::string::npos);
    assert(decode_log.find("ALSA lib") == std::string::npos);
  }
#endif

  {
    JsonRpcEndpoint concurrent_endpoint;
    assert(concurrent_endpoint.addMethod("test.sleep", [](const JsonValue&) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      JsonValue result = JsonValue::object();
      result["ok"] = true;
      return result;
    }));

    const uint16_t port = findFreePort(drivers.socket());
    std::exception_ptr server_error;
    std::thread server_thread([&] {
      try {
        audio_studio::rpc::RpcSocketServer server(drivers.socket(), concurrent_endpoint);
        server.serve("127.0.0.1", port, {2, 5000});
      } catch (...) {
        server_error = std::current_exception();
      }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const auto begin = std::chrono::steady_clock::now();
    std::thread first_client([&] {
      audio_studio::rpc::SocketJsonRpcTransport socket_transport(drivers.socket(), {"127.0.0.1", port, 5000});
      JsonRpcClient socket_client(socket_transport);
      assert(socket_client.call("test.sleep").at("ok").asBool());
    });
    std::thread second_client([&] {
      audio_studio::rpc::SocketJsonRpcTransport socket_transport(drivers.socket(), {"127.0.0.1", port, 5000});
      JsonRpcClient socket_client(socket_transport);
      assert(socket_client.call("test.sleep").at("ok").asBool());
    });
    first_client.join();
    second_client.join();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin);
    assert(elapsed.count() < 900);

    server_thread.join();
    throwIfThreadFailed(server_error);
  }

  {
    audio_studio::framework::audio::AudioService transport_audio_service;
    JsonRpcEndpoint transport_endpoint;
    audio_studio::rpc::registerServerHealthRpcMethod(transport_endpoint);
    auto transport_context = std::make_shared<audio_studio::rpc::RpcRuntimeContext>(transport_audio_service);
    audio_studio::rpc::registerAudioStudioRpcMethods(transport_endpoint, transport_context);

    const uint16_t port = findFreePort(drivers.socket());
    std::exception_ptr server_error;
    std::thread server_thread([&] {
      try {
        audio_studio::rpc::RpcSocketServer server(drivers.socket(), transport_endpoint, [&](const audio_studio::rpc::RpcBinaryFrame& request) {
          return audioServiceStreamHandler(*transport_context, request);
        });
        server.serve("127.0.0.1", port, {8, 5000});
      } catch (...) {
        server_error = std::current_exception();
      }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    audio_studio::rpc::SocketJsonRpcTransport socket_transport(drivers.socket(), {"127.0.0.1", port, 5000});
    audio_studio::rpc::SocketRpcStreamTransport socket_stream(drivers.socket(), {"127.0.0.1", port, 5000});
    JsonRpcClient socket_client(socket_transport);
    audio_studio::rpc::AudioRpcClient socket_audio(socket_client, &socket_stream);
    assert(socket_client.call("server.health").at("ok").asBool());
    auto socket_playback = socket_audio.createPlaybackSession(makePlaybackConfig("socket-playback"));
    assert(socket_playback.sessionId() == "socket-playback");
    socket_playback.start();
    const std::vector<uint8_t> payload(8192, 0x5a);
    auto socket_written = socket_playback.writeFrames(payload);
    assert(socket_written.accepted);
    assert(socket_written.bytes == payload.size());
    assert(socket_stream.isOpen());
    const std::vector<uint8_t> second_payload(128, 0x33);
    auto socket_written_again = socket_playback.writeFrames(second_payload);
    assert(socket_written_again.accepted);
    assert(socket_written_again.bytes == second_payload.size());
    assert(socket_stream.isOpen());
    auto socket_capture = socket_audio.createCaptureSession(makeCaptureConfig("socket-capture"));
    socket_capture.start();
    auto socket_read = socket_capture.readFrames(4096);
    assert(socket_read.ok);
    assert(socket_read.bytes == 4096);
    assert(socket_read.data.size() == 4096);

    server_thread.join();
    throwIfThreadFailed(server_error);
  }

  {
    audio_studio::framework::audio::AudioService transport_audio_service;
    JsonRpcEndpoint transport_endpoint;
    audio_studio::rpc::registerServerHealthRpcMethod(transport_endpoint);
    auto transport_context = std::make_shared<audio_studio::rpc::RpcRuntimeContext>(transport_audio_service);
    audio_studio::rpc::registerAudioStudioRpcMethods(transport_endpoint, transport_context);

    const std::string base = "/tmp/audio-studio-rpc-test-" + std::to_string(static_cast<long long>(getpid()));
    const std::string request_pipe = base + ".req";
    const std::string response_pipe = base + ".rsp";
    std::exception_ptr server_error;
    std::thread server_thread([&] {
      try {
        audio_studio::rpc::RpcPipeServer server(drivers.pipe(), transport_endpoint, [&](const audio_studio::rpc::RpcBinaryFrame& request) {
          return audioServiceStreamHandler(*transport_context, request);
        });
        server.serve(request_pipe, response_pipe, {8, 5000});
      } catch (...) {
        server_error = std::current_exception();
      }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    audio_studio::rpc::PipeJsonRpcTransport pipe_transport(drivers.pipe(), {request_pipe, response_pipe, 5000});
    audio_studio::rpc::PipeRpcStreamTransport pipe_stream(drivers.pipe(), {request_pipe, response_pipe, 5000});
    JsonRpcClient pipe_client(pipe_transport);
    audio_studio::rpc::AudioRpcClient pipe_audio(pipe_client, &pipe_stream);
    assert(pipe_client.call("server.health").at("ok").asBool());
    auto pipe_playback = pipe_audio.createPlaybackSession(makePlaybackConfig("pipe-playback"));
    assert(pipe_playback.sessionId() == "pipe-playback");
    pipe_playback.start();
    const std::vector<uint8_t> payload(4096, 0xa5);
    auto pipe_written = pipe_playback.writeFrames(payload);
    assert(pipe_written.accepted);
    assert(pipe_written.bytes == payload.size());
    assert(pipe_stream.isOpen());
    const std::vector<uint8_t> second_payload(64, 0x3c);
    auto pipe_written_again = pipe_playback.writeFrames(second_payload);
    assert(pipe_written_again.accepted);
    assert(pipe_written_again.bytes == second_payload.size());
    assert(pipe_stream.isOpen());
    auto pipe_capture = pipe_audio.createCaptureSession(makeCaptureConfig("pipe-capture"));
    pipe_capture.start();
    auto pipe_read = pipe_capture.readFrames(2048);
    assert(pipe_read.ok);
    assert(pipe_read.bytes == 2048);
    assert(pipe_read.data.size() == 2048);

    server_thread.join();
    throwIfThreadFailed(server_error);
  }

  drivers.shutdown();

  return 0;
}
