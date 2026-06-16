#include <iostream>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "autoconfig.h"

#if defined(CONFIG_RPC)
#include "json_rpc.hpp"
#include "rpc_api_registry.hpp"
#include "rpc_runtime_context.hpp"
#include "rpc_server.hpp"
#if defined(CONFIG_DRIVERS_CORE)
#include "driver_manager.hpp"
#endif
#if defined(CONFIG_RPC_AUDIO_METHODS)
#include "audio_studio/framework/audio/audio_service.hpp"
#include "audio_rpc.hpp"
#endif
#endif

namespace {

std::string valueAfter(int argc, char** argv, const std::string& flag, const std::string& fallback) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == flag) return argv[i + 1];
  }
  return fallback;
}

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

#if defined(CONFIG_RPC)
#if defined(CONFIG_RPC_AUDIO_METHODS)
audio_studio::framework::audio::AudioService& audioService() {
  static audio_studio::framework::audio::AudioService service;
  return service;
}

struct RpcEndpointBundle {
  audio_studio::rpc::JsonRpcEndpoint endpoint;
  std::shared_ptr<audio_studio::rpc::RpcRuntimeContext> context;
};
#endif

audio_studio::rpc::RpcBinaryFrame errorFrame(const audio_studio::rpc::RpcBinaryFrame& request, const std::string& message) {
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

#if defined(CONFIG_RPC_AUDIO_METHODS)
audio_studio::rpc::RpcBinaryFrame audioStreamResponse(audio_studio::rpc::RpcRuntimeContext& context,
                                                      const audio_studio::rpc::RpcBinaryFrame& request) {
  using namespace audio_studio;
  if (request.header.service_id != static_cast<uint16_t>(rpc::RpcServiceId::kAudio)) {
    return rpc::makeDefaultStreamAck(request);
  }

  std::string session_id;
  if (!context.sessionIdForNumeric(request.header.session_id, session_id)) {
    return errorFrame(request, "audio stream session not found for numeric session: " + std::to_string(request.header.session_id));
  }

  const uint32_t timeout_ms = request.header.flags == 0 ? 5000 : request.header.flags;
  if (request.header.method_id == rpc::kRpcAudioMethodWriteFrames) {
    size_t accepted_bytes = 0;
    auto status = context.audio().writeFrames(session_id, request.payload, timeout_ms, accepted_bytes);
    if (!status.ok()) return errorFrame(request, status.message());

    rpc::JsonValue payload = rpc::JsonValue::object();
    payload["accepted"] = true;
    payload["accepted_bytes"] = static_cast<uint32_t>(accepted_bytes);
    payload["queued_bytes"] = static_cast<uint32_t>(0);
    payload["credit_bytes"] = static_cast<uint32_t>(1024 * 1024);
    payload["request_id"] = request.header.request_id;
    const std::string json = payload.dump();

    rpc::RpcBinaryFrame ack;
    ack.header.message_type = rpc::RpcMessageType::kStreamAck;
    ack.header.service_id = request.header.service_id;
    ack.header.method_id = request.header.method_id;
    ack.header.payload_type = rpc::RpcPayloadType::kJson;
    ack.header.request_id = request.header.request_id;
    ack.header.session_id = request.header.session_id;
    ack.header.stream_id = request.header.stream_id;
    ack.payload.assign(json.begin(), json.end());
    return ack;
  }

  if (request.header.method_id == rpc::kRpcAudioMethodReadFrames) {
    size_t max_bytes = 65536;
    if (!request.payload.empty()) {
      try {
        const std::string json(request.payload.begin(), request.payload.end());
        const auto params = rpc::parseJson(json);
        max_bytes = rpc::optionalUInt32Param(params, "max_bytes", 65536);
      } catch (const std::exception& error) {
        return errorFrame(request, error.what());
      }
    }

    std::vector<uint8_t> data;
    auto status = context.audio().readFrames(session_id, max_bytes, timeout_ms, data);
    if (!status.ok()) return errorFrame(request, status.message());

    rpc::RpcBinaryFrame frame;
    frame.header.message_type = rpc::RpcMessageType::kStreamData;
    frame.header.service_id = request.header.service_id;
    frame.header.method_id = request.header.method_id;
    frame.header.payload_type = rpc::RpcPayloadType::kBinary;
    frame.header.request_id = request.header.request_id;
    frame.header.session_id = request.header.session_id;
    frame.header.stream_id = request.header.stream_id;
    frame.payload = std::move(data);
    return frame;
  }

  return errorFrame(request, "unsupported audio stream method id: " + std::to_string(request.header.method_id));
}
#endif

audio_studio::rpc::JsonRpcEndpoint makeEndpoint(audio_studio::rpc::RpcStreamDefaults stream_defaults = {}) {
  audio_studio::rpc::JsonRpcEndpoint endpoint;
#if defined(CONFIG_RPC_AUDIO_METHODS)
  auto context = std::make_shared<audio_studio::rpc::RpcRuntimeContext>(audioService(), std::move(stream_defaults));
  audio_studio::rpc::registerAudioStudioRpcMethods(endpoint, context);
#else
  audio_studio::rpc::registerServerHealthRpcMethod(endpoint);
#endif
  return endpoint;
}

#if defined(CONFIG_RPC_AUDIO_METHODS)
RpcEndpointBundle makeEndpointBundle(audio_studio::rpc::RpcStreamDefaults stream_defaults = {}) {
  RpcEndpointBundle bundle;
  bundle.context = std::make_shared<audio_studio::rpc::RpcRuntimeContext>(audioService(), std::move(stream_defaults));
  audio_studio::rpc::registerAudioStudioRpcMethods(bundle.endpoint, bundle.context);
  return bundle;
}
#endif

std::string handleRpcOnce(const std::string& request_json) {
#if defined(CONFIG_RPC_AUDIO_METHODS)
  auto bundle = makeEndpointBundle();
  return bundle.endpoint.handleRequest(request_json);
#else
  auto endpoint = makeEndpoint();
  return endpoint.handleRequest(request_json);
#endif
}

size_t maxRequestsFromArgs(int argc, char** argv, size_t fallback) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "--max-requests") return static_cast<size_t>(std::stoull(argv[i + 1]));
  }
  return fallback;
}
#endif

} // namespace

int main(int argc, char** argv) {
  const std::string arg = argc > 1 ? argv[1] : "--rpc";
  if (arg == "--version") {
    std::cout << "Audio Studio as_server initial " << toolOs() << "/" << targetPlatform() << "\n";
    return 0;
  }
  if (arg == "--health") {
    std::cout << "{\"ok\":true,\"tool_os\":\"" << toolOs() << "\",\"platform\":\"" << targetPlatform()
              << "\"}\n";
    return 0;
  }
#if defined(CONFIG_RPC)
  if (arg == "--rpc-once") {
    if (argc < 3) {
      std::cerr << "usage: as_server --rpc-once '<json-rpc-request>'\n";
      return 2;
    }
    std::cout << handleRpcOnce(argv[2]) << "\n";
    return 0;
  }
#if defined(CONFIG_RPC_SERVER) && defined(CONFIG_DRIVERS_CORE)
  if (arg == "--rpc") {
    auto& drivers = audio_studio::drivers::DriverManager::instance();
    auto status = drivers.initialize();
    if (!status.ok()) {
      std::cerr << status.toJson() << "\n";
      return 1;
    }
    try {
      audio_studio::rpc::RpcStreamDefaults stream_defaults;
      const std::string transport = argc > 2 && std::string(argv[2]).rfind("--", 0) != 0 ? argv[2] : "socket";
#if defined(CONFIG_RPC_AUDIO_METHODS) && defined(CONFIG_DRIVER_AUDIO)
      audioService().configureDeviceRegistry(&drivers.audioRegistry());
#endif
#if defined(CONFIG_RPC_TRANSPORT_SOCKET)
      if (transport == "socket") {
        const std::string positional_host = argc > 3 && std::string(argv[3]).rfind("--", 0) != 0 ? argv[3] : "127.0.0.1";
        const std::string positional_port = argc > 4 && std::string(argv[4]).rfind("--", 0) != 0 ? argv[4] : "9900";
        const std::string host = valueAfter(argc, argv, "--host", positional_host);
        const uint16_t port = static_cast<uint16_t>(std::stoul(valueAfter(argc, argv, "--port", positional_port)));
        stream_defaults.host = host;
        stream_defaults.port = port;
        stream_defaults.stream_uri_base = "tcp://" + host + ":" + std::to_string(port);
        auto bundle = makeEndpointBundle(std::move(stream_defaults));
        audio_studio::rpc::RpcSocketServer server(drivers.socket(), bundle.endpoint, [context = bundle.context](const audio_studio::rpc::RpcBinaryFrame& request) {
          return audioStreamResponse(*context, request);
        });
        server.serve(host, port, {maxRequestsFromArgs(argc, argv, 0), 5000});
        drivers.shutdown();
        return 0;
      }
#endif
#if defined(CONFIG_RPC_TRANSPORT_PIPE)
      if (transport == "pipe") {
        const std::string request_pipe = valueAfter(argc, argv, "--request-pipe", argc > 3 ? argv[3] : "");
        const std::string response_pipe = valueAfter(argc, argv, "--response-pipe", argc > 4 ? argv[4] : "");
        if (request_pipe.empty() || response_pipe.empty()) {
          std::cerr << "usage: as_server --rpc pipe <request-fifo> <response-fifo> [--max-requests N]\n";
          drivers.shutdown();
          return 2;
        }
        stream_defaults.stream_uri_base = "pipe://" + request_pipe + ":" + response_pipe;
        auto bundle = makeEndpointBundle(std::move(stream_defaults));
        audio_studio::rpc::RpcPipeServer server(drivers.pipe(), bundle.endpoint, [context = bundle.context](const audio_studio::rpc::RpcBinaryFrame& request) {
          return audioStreamResponse(*context, request);
        });
        server.serve(request_pipe, response_pipe, {maxRequestsFromArgs(argc, argv, 0), 5000});
        drivers.shutdown();
        return 0;
      }
#endif
      std::cerr << "unsupported RPC transport: " << transport << "\n";
      drivers.shutdown();
      return 2;
    } catch (const std::exception& error) {
      std::cerr << "{\"ok\":false,\"error\":\"" << error.what() << "\"}\n";
      drivers.shutdown();
      return 1;
    }
  }
#endif
#else
  if (arg == "--rpc-once") {
    std::cerr << "as_server was built without CONFIG_RPC\n";
    return 2;
  }
#endif
  std::cerr << "usage: as_server [--version|--health|--rpc-once|--rpc]\n";
  return 2;
}
