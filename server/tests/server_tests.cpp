#include <cassert>
#include <chrono>
#include <string>
#include <thread>
#include <unistd.h>

#include "audio_studio/framework/audio/audio_service.hpp"
#include "audio_studio/framework/service_registry.hpp"
#include "audio_studio/framework/status.hpp"
#include "audio_studio/rpc/audio_rpc.hpp"
#include "audio_studio/rpc/json_rpc.hpp"
#include "audio_studio/rpc/json_value.hpp"
#include "audio_studio/rpc/rpc_pipe_transport.hpp"
#include "audio_studio/rpc/rpc_server.hpp"
#include "audio_studio/rpc/rpc_socket_transport.hpp"
#include "driver_manager.hpp"

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

audio_studio::rpc::JsonValue makePlaybackParams(const std::string& id) {
  audio_studio::rpc::JsonValue params = audio_studio::rpc::JsonValue::object();
  params["session_id"] = id;
  params["sample_rate"] = 48000;
  params["channels"] = 2;
  params["bytes_per_sample"] = 2;
  return params;
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

  JsonValue create_params = JsonValue::object();
  create_params["session_id"] = "playback-1";
  create_params["sample_rate"] = 48000;
  create_params["channels"] = 2;
  create_params["bytes_per_sample"] = 2;
  JsonValue created = audio_client.call("audio.createPlaybackSession", create_params);
  assert(created.at("session").at("session_id").asString() == "playback-1");
  assert(!created.at("session").at("running").asBool());

  JsonValue session_params = JsonValue::object();
  session_params["session_id"] = "playback-1";
  JsonValue started = audio_client.call("audio.start", session_params);
  assert(started.at("session").at("prepared").asBool());
  assert(started.at("session").at("running").asBool());

  JsonValue write_params = JsonValue::object();
  write_params["session_id"] = "playback-1";
  write_params["bytes"] = 4096;
  JsonValue written = audio_client.call("audio.writeFrames", write_params);
  assert(written.at("accepted").asBool());
  assert(written.at("bytes").asInt64() == 4096);

  JsonValue listed = audio_client.call("audio.listSessions");
  assert(listed.at("sessions").asArray().size() == 1);

  JsonValue stopped = audio_client.call("audio.stop", session_params);
  assert(!stopped.at("session").at("running").asBool());

  JsonValue closed = audio_client.call("audio.closeSession", session_params);
  assert(closed.at("closed").asBool());
  assert(audio_client.call("audio.listSessions").at("sessions").asArray().empty());

  auto& drivers = audio_studio::drivers::DriverManager::instance();
  assert(drivers.initialize().ok());

  {
    audio_studio::framework::audio::AudioService transport_audio_service;
    JsonRpcEndpoint transport_endpoint;
    audio_studio::rpc::registerServerHealthRpcMethod(transport_endpoint);
    audio_studio::rpc::registerAudioRpcMethods(transport_endpoint, transport_audio_service);

    const uint16_t port = findFreePort(drivers.socket());
    std::exception_ptr server_error;
    std::thread server_thread([&] {
      try {
        audio_studio::rpc::RpcSocketServer server(drivers.socket(), transport_endpoint);
        server.serve("127.0.0.1", port, {2, 5000});
      } catch (...) {
        server_error = std::current_exception();
      }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    audio_studio::rpc::SocketJsonRpcTransport socket_transport(drivers.socket(), {"127.0.0.1", port, 5000});
    JsonRpcClient socket_client(socket_transport);
    assert(socket_client.call("server.health").at("ok").asBool());
    JsonValue socket_created = socket_client.call("audio.createPlaybackSession", makePlaybackParams("socket-playback"));
    assert(socket_created.at("session").at("session_id").asString() == "socket-playback");

    server_thread.join();
    throwIfThreadFailed(server_error);
  }

  {
    audio_studio::framework::audio::AudioService transport_audio_service;
    JsonRpcEndpoint transport_endpoint;
    audio_studio::rpc::registerServerHealthRpcMethod(transport_endpoint);
    audio_studio::rpc::registerAudioRpcMethods(transport_endpoint, transport_audio_service);

    const std::string base = "/tmp/audio-studio-rpc-test-" + std::to_string(static_cast<long long>(getpid()));
    const std::string request_pipe = base + ".req";
    const std::string response_pipe = base + ".rsp";
    std::exception_ptr server_error;
    std::thread server_thread([&] {
      try {
        audio_studio::rpc::RpcPipeServer server(drivers.pipe(), transport_endpoint);
        server.serve(request_pipe, response_pipe, {2, 5000});
      } catch (...) {
        server_error = std::current_exception();
      }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    audio_studio::rpc::PipeJsonRpcTransport pipe_transport(drivers.pipe(), {request_pipe, response_pipe, 5000});
    JsonRpcClient pipe_client(pipe_transport);
    assert(pipe_client.call("server.health").at("ok").asBool());
    JsonValue pipe_created = pipe_client.call("audio.createPlaybackSession", makePlaybackParams("pipe-playback"));
    assert(pipe_created.at("session").at("session_id").asString() == "pipe-playback");

    server_thread.join();
    throwIfThreadFailed(server_error);
  }

  drivers.shutdown();

  return 0;
}
