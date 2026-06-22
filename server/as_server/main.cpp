#include <iostream>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <CLI/CLI.hpp>

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
#include "audio_service.hpp"
#include "audio_rpc.hpp"
#endif
#if defined(CONFIG_FRAMEWORK_CONFIG)
#include "config_service.hpp"
#endif
#if defined(CONFIG_FRAMEWORK_LOG)
#include "log_service.hpp"
#endif
#if defined(CONFIG_FRAMEWORK_TRANSPORT)
#include "transport_manager.hpp"
#endif
#if defined(CONFIG_PLATFORM_SIMULATOR)
#include "simulator_pipe_datalink_device.hpp"
#endif
#endif

namespace {

struct ServerOptions {
  bool version = false;
  bool health = false;
  bool rpc = false;
  std::string rpc_once;
  std::string host = "127.0.0.1";
  uint16_t port = 9900;
  std::string request_pipe;
  std::string response_pipe;
  size_t max_requests = 0;
  std::string log_driver_factory;
  std::string log_source;
  std::string log_level;
  std::string log_datalink_endpoint;
  std::string log_datalink_rx;
  std::string log_datalink_tx;
  uint32_t log_datalink_mtu = 0;
  std::string log_trace_ldc;
  std::string audio_driver_factory;
  std::string audio_device_name;
  std::string audio_datalink_endpoint;
  std::string audio_datalink_rx;
  std::string audio_datalink_tx;
  uint32_t audio_datalink_mtu = 0;
  std::vector<std::string> rpc_args;
};

int parseServerOptions(int argc, char** argv, ServerOptions& options) {
  CLI::App app{"Audio Studio server", "as_server"};
  app.option_defaults()->always_capture_default();
  app.add_flag("--version", options.version, "Print version and build target");
  app.add_flag("--health", options.health, "Print local health JSON");
  app.add_flag("--rpc", options.rpc, "Run JSON-RPC server. Default when no command is selected");
  app.add_option("--rpc-once", options.rpc_once, "Handle one JSON-RPC request and exit");
  app.add_option("--host", options.host, "RPC socket bind host");
  app.add_option("--port", options.port, "RPC socket bind port");
  app.add_option("--request-pipe", options.request_pipe, "RPC request FIFO path");
  app.add_option("--response-pipe", options.response_pipe, "RPC response FIFO path");
  app.add_option("--max-requests", options.max_requests, "Stop after N requests. Zero means forever");
  app.add_option("--log-driver-factory", options.log_driver_factory, "Default log driver factory for RPC log sessions");
  app.add_option("--log-source", options.log_source, "Default log source for RPC log sessions");
  app.add_option("--log-level", options.log_level, "Default minimum log level for RPC log sessions");
  app.add_option("--log-datalink-endpoint", options.log_datalink_endpoint, "Default log data-link endpoint prefix");
  app.add_option("--log-datalink-rx", options.log_datalink_rx, "Default log data-link RX file");
  app.add_option("--log-datalink-tx", options.log_datalink_tx, "Default log data-link TX file");
  app.add_option("--log-datalink-mtu", options.log_datalink_mtu, "Default log data-link MTU");
  app.add_option("--log-trace-ldc", options.log_trace_ldc, "Default SOF trace LDC dictionary for log decoding");
  app.add_option("--audio-driver-factory", options.audio_driver_factory, "Default audio driver factory for RPC audio sessions");
  app.add_option("--audio-device", options.audio_device_name, "Default audio stream/device name for RPC audio sessions");
  app.add_option("--audio-datalink-endpoint", options.audio_datalink_endpoint, "Default audio data-link endpoint prefix");
  app.add_option("--audio-datalink-rx", options.audio_datalink_rx, "Default audio data-link RX file");
  app.add_option("--audio-datalink-tx", options.audio_datalink_tx, "Default audio data-link TX file");
  app.add_option("--audio-datalink-mtu", options.audio_datalink_mtu, "Default audio data-link MTU");
  app.add_option("rpc-args", options.rpc_args, "Optional transport and positional transport args");
  app.allow_extras(false);

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& error) {
    return app.exit(error);
  }
  return -1;
}

std::string positionalOr(const std::vector<std::string>& values, size_t index, const std::string& fallback) {
  return index < values.size() ? values[index] : fallback;
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

#if defined(CONFIG_FRAMEWORK_CONFIG)
audio_studio::framework::config::ConfigService& configService() {
  static audio_studio::framework::config::ConfigService service;
  return service;
}
#endif

#if defined(CONFIG_FRAMEWORK_LOG)
audio_studio::framework::log::LogService& logService() {
  static audio_studio::framework::log::LogService service;
  return service;
}

audio_studio::framework::log::LogSessionConfig defaultLogConfigFromOptions(const ServerOptions& options) {
  audio_studio::framework::log::LogSessionConfig config;
  config.driver_factory = options.log_driver_factory;
  config.source = options.log_source;
  config.min_level = options.log_level;
  if (!options.log_trace_ldc.empty()) config.options["trace_ldc"] = options.log_trace_ldc;
  return config;
}
#endif

audio_studio::framework::audio::AudioServiceConfig defaultAudioConfigFromOptions(const ServerOptions& options) {
  audio_studio::framework::audio::AudioServiceConfig config;
  config.driver_factory = options.audio_driver_factory;
  config.default_device_name = options.audio_device_name;
  return config;
}

bool hasLogDataLinkOptions(const ServerOptions& options) {
  return !options.log_datalink_endpoint.empty() ||
         !options.log_datalink_rx.empty() ||
         !options.log_datalink_tx.empty() ||
         options.log_datalink_mtu > 0;
}

bool hasAudioDataLinkOptions(const ServerOptions& options) {
  return !options.audio_datalink_endpoint.empty() ||
         !options.audio_datalink_rx.empty() ||
         !options.audio_datalink_tx.empty() ||
         options.audio_datalink_mtu > 0;
}

#if defined(CONFIG_FRAMEWORK_TRANSPORT)
audio_studio::drivers::datalink::DataLinkDeviceConfig dataLinkConfigFromOptions(const std::string& endpoint,
                                                                                const std::string& rx_path,
                                                                                const std::string& tx_path,
                                                                                uint32_t mtu) {
  audio_studio::drivers::datalink::DataLinkDeviceConfig config;
  config.name = "rv32qemu-datalink";
  config.endpoint = endpoint;
  if (!endpoint.empty()) config.options["endpoint"] = endpoint;
  if (!rx_path.empty()) config.options["rx_path"] = rx_path;
  if (!tx_path.empty()) config.options["tx_path"] = tx_path;
  if (mtu > 0) config.options["mtu"] = std::to_string(mtu);
  return config;
}

bool sameDataLinkConfig(const audio_studio::drivers::datalink::DataLinkDeviceConfig& lhs,
                        const audio_studio::drivers::datalink::DataLinkDeviceConfig& rhs) {
  return lhs.name == rhs.name && lhs.endpoint == rhs.endpoint && lhs.options == rhs.options;
}

audio_studio::framework::Status configureTransportDataLinkFromOptions(const ServerOptions& options) {
  const bool has_log = hasLogDataLinkOptions(options);
  const bool has_audio = hasAudioDataLinkOptions(options);
  if (!has_log && !has_audio) return audio_studio::framework::Status::success();

#if defined(CONFIG_PLATFORM_SIMULATOR)
  auto config = has_audio
                  ? dataLinkConfigFromOptions(options.audio_datalink_endpoint,
                                              options.audio_datalink_rx,
                                              options.audio_datalink_tx,
                                              options.audio_datalink_mtu)
                  : dataLinkConfigFromOptions(options.log_datalink_endpoint,
                                              options.log_datalink_rx,
                                              options.log_datalink_tx,
                                              options.log_datalink_mtu);
  if (has_log && has_audio) {
    const auto log_config = dataLinkConfigFromOptions(options.log_datalink_endpoint,
                                                     options.log_datalink_rx,
                                                     options.log_datalink_tx,
                                                     options.log_datalink_mtu);
    if (!sameDataLinkConfig(config, log_config)) {
      return audio_studio::framework::Status::invalidArgument("log/audio data-link options do not match");
    }
  }

  audio_studio::platform::simulator::ensureSimulatorPipeDataLinkDeviceRegistered();
  audio_studio::framework::transport::DataLinkManagerConfig manager_config;
  manager_config.ack_timeout_ms = 1000;
  manager_config.max_retries = 3;
  return audio_studio::framework::transport::TransportManager::instance().configureDataLinkDevice(
      "simulator-pipe", config, manager_config);
#else
  return audio_studio::framework::Status::invalidArgument("data-link options require simulator platform support");
#endif
}
#endif

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
#if defined(CONFIG_FRAMEWORK_CONFIG)
  context->setConfigService(&configService());
#endif
#if defined(CONFIG_FRAMEWORK_LOG)
  context->setLogService(&logService());
#endif
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
#if defined(CONFIG_FRAMEWORK_CONFIG)
  bundle.context->setConfigService(&configService());
#endif
#if defined(CONFIG_FRAMEWORK_LOG)
  bundle.context->setLogService(&logService());
#endif
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

#endif

} // namespace

int main(int argc, char** argv) {
  ServerOptions options;
  const int parse_result = parseServerOptions(argc, argv, options);
  if (parse_result >= 0) return parse_result;

  if (options.version) {
    std::cout << "Audio Studio as_server initial " << toolOs() << "/" << targetPlatform() << "\n";
    return 0;
  }
  if (options.health) {
    std::cout << "{\"ok\":true,\"tool_os\":\"" << toolOs() << "\",\"platform\":\"" << targetPlatform()
              << "\"}\n";
    return 0;
  }
#if defined(CONFIG_RPC)
  if (!options.rpc_once.empty()) {
#if defined(CONFIG_DRIVERS_CORE) && (defined(CONFIG_FRAMEWORK_CONFIG) || defined(CONFIG_FRAMEWORK_LOG))
    auto& drivers = audio_studio::drivers::DriverManager::instance();
    auto status = drivers.initialize();
    if (!status.ok()) {
      std::cerr << status.toJson() << "\n";
      return 1;
    }
#if defined(CONFIG_FRAMEWORK_CONFIG)
    configService().setDrivers(&drivers.filesystem(), &drivers.os(), &drivers.dynlib());
#endif
#if defined(CONFIG_RPC_AUDIO_METHODS) && defined(CONFIG_FRAMEWORK_TRANSPORT)
    status = configureTransportDataLinkFromOptions(options);
    if (!status.ok()) {
      std::cerr << status.toJson() << "\n";
      drivers.shutdown();
      return 1;
    }
#endif
#if defined(CONFIG_RPC_AUDIO_METHODS) && defined(CONFIG_DRIVER_AUDIO)
    audioService().configureDeviceRegistry(&drivers.audioRegistry(), defaultAudioConfigFromOptions(options));
#endif
#if defined(CONFIG_FRAMEWORK_LOG) && defined(CONFIG_DRIVER_LOG)
    logService().configureDeviceRegistry(&drivers.logRegistry());
#endif
#if defined(CONFIG_FRAMEWORK_LOG)
    logService().setDefaultSessionConfig(defaultLogConfigFromOptions(options));
#endif
    std::cout << handleRpcOnce(options.rpc_once) << "\n";
    drivers.shutdown();
    return 0;
#else
    std::cout << handleRpcOnce(options.rpc_once) << "\n";
    return 0;
#endif
  }
#if defined(CONFIG_RPC_SERVER) && defined(CONFIG_DRIVERS_CORE)
  if (options.rpc || (!options.version && !options.health && options.rpc_once.empty())) {
    auto& drivers = audio_studio::drivers::DriverManager::instance();
    auto status = drivers.initialize();
    if (!status.ok()) {
      std::cerr << status.toJson() << "\n";
      return 1;
    }
    try {
      audio_studio::rpc::RpcStreamDefaults stream_defaults;
      const std::string transport = positionalOr(options.rpc_args, 0, "socket");
#if defined(CONFIG_RPC_AUDIO_METHODS) && defined(CONFIG_FRAMEWORK_TRANSPORT)
      status = configureTransportDataLinkFromOptions(options);
      if (!status.ok()) {
        std::cerr << status.toJson() << "\n";
        drivers.shutdown();
        return 1;
      }
#endif
#if defined(CONFIG_RPC_AUDIO_METHODS) && defined(CONFIG_DRIVER_AUDIO)
      audioService().configureDeviceRegistry(&drivers.audioRegistry(), defaultAudioConfigFromOptions(options));
#endif
#if defined(CONFIG_FRAMEWORK_CONFIG) && defined(CONFIG_DRIVER_FILESYSTEM) && defined(CONFIG_DRIVER_OS) && defined(CONFIG_DRIVER_DYNLIB)
      configService().setDrivers(&drivers.filesystem(), &drivers.os(), &drivers.dynlib());
#endif
#if defined(CONFIG_FRAMEWORK_LOG) && defined(CONFIG_DRIVER_LOG)
      logService().configureDeviceRegistry(&drivers.logRegistry());
#endif
#if defined(CONFIG_FRAMEWORK_LOG)
      logService().setDefaultSessionConfig(defaultLogConfigFromOptions(options));
#endif
#if defined(CONFIG_RPC_TRANSPORT_SOCKET)
      if (transport == "socket") {
        const std::string host = options.host == "127.0.0.1" ? positionalOr(options.rpc_args, 1, options.host) : options.host;
        const uint16_t port = options.port == 9900
                                ? static_cast<uint16_t>(std::stoul(positionalOr(options.rpc_args, 2, std::to_string(options.port))))
                                : options.port;
        stream_defaults.host = host;
        stream_defaults.port = port;
        stream_defaults.stream_uri_base = "tcp://" + host + ":" + std::to_string(port);
        auto bundle = makeEndpointBundle(std::move(stream_defaults));
        audio_studio::rpc::RpcSocketServer server(drivers.socket(), bundle.endpoint, [context = bundle.context](const audio_studio::rpc::RpcBinaryFrame& request) {
          return audioStreamResponse(*context, request);
        });
        server.serve(host, port, {options.max_requests, 5000});
        drivers.shutdown();
        return 0;
      }
#endif
#if defined(CONFIG_RPC_TRANSPORT_PIPE)
      if (transport == "pipe") {
        const std::string request_pipe = options.request_pipe.empty() ? positionalOr(options.rpc_args, 1, "") : options.request_pipe;
        const std::string response_pipe = options.response_pipe.empty() ? positionalOr(options.rpc_args, 2, "") : options.response_pipe;
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
        server.serve(request_pipe, response_pipe, {options.max_requests, 5000});
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
  if (!options.rpc_once.empty()) {
    std::cerr << "as_server was built without CONFIG_RPC\n";
    return 2;
  }
#endif
  std::cerr << "usage: as_server [--version|--health|--rpc-once|--rpc]\n";
  return 2;
}
