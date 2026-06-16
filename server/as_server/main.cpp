#include <iostream>
#include <cstdint>
#include <string>

#include "autoconfig.h"

#if defined(CONFIG_RPC)
#include "audio_studio/rpc/json_rpc.hpp"
#include "audio_studio/rpc/rpc_server.hpp"
#if defined(CONFIG_DRIVERS_CORE)
#include "driver_manager.hpp"
#endif
#if defined(CONFIG_RPC_AUDIO_METHODS)
#include "audio_studio/framework/audio/audio_service.hpp"
#include "audio_studio/rpc/audio_rpc.hpp"
#endif
#endif

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

#if defined(CONFIG_RPC)
audio_studio::rpc::JsonRpcEndpoint makeEndpoint() {
  audio_studio::rpc::JsonRpcEndpoint endpoint;
  audio_studio::rpc::registerServerHealthRpcMethod(endpoint);
#if defined(CONFIG_RPC_AUDIO_METHODS)
  static audio_studio::framework::audio::AudioService audio_service;
  audio_studio::rpc::registerAudioRpcMethods(endpoint, audio_service);
#endif
  return endpoint;
}

std::string handleRpcOnce(const std::string& request_json) {
  auto endpoint = makeEndpoint();
  return endpoint.handleRequest(request_json);
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
  const std::string arg = argc > 1 ? argv[1] : "--version";
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
    if (argc < 3) {
      std::cerr << "usage: as_server --rpc socket <host> <port> [--max-requests N]\n"
                << "       as_server --rpc pipe <request-fifo> <response-fifo> [--max-requests N]\n";
      return 2;
    }
    auto& drivers = audio_studio::drivers::DriverManager::instance();
    auto status = drivers.initialize();
    if (!status.ok()) {
      std::cerr << status.toJson() << "\n";
      return 1;
    }
    try {
      auto endpoint = makeEndpoint();
#if defined(CONFIG_RPC_TRANSPORT_SOCKET)
      if (std::string(argv[2]) == "socket") {
        if (argc < 5) {
          std::cerr << "usage: as_server --rpc socket <host> <port> [--max-requests N]\n";
          drivers.shutdown();
          return 2;
        }
        audio_studio::rpc::RpcSocketServer server(drivers.socket(), endpoint);
        server.serve(argv[3], static_cast<uint16_t>(std::stoul(argv[4])), {maxRequestsFromArgs(argc, argv, 0), 5000});
        drivers.shutdown();
        return 0;
      }
#endif
#if defined(CONFIG_RPC_TRANSPORT_PIPE)
      if (std::string(argv[2]) == "pipe") {
        if (argc < 5) {
          std::cerr << "usage: as_server --rpc pipe <request-fifo> <response-fifo> [--max-requests N]\n";
          drivers.shutdown();
          return 2;
        }
        audio_studio::rpc::RpcPipeServer server(drivers.pipe(), endpoint);
        server.serve(argv[3], argv[4], {maxRequestsFromArgs(argc, argv, 0), 5000});
        drivers.shutdown();
        return 0;
      }
#endif
      std::cerr << "unsupported RPC transport: " << argv[2] << "\n";
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
