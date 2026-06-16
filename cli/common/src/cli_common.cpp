#include "audio_studio/cli/cli_common.hpp"

#include <iostream>
#include <sstream>

#include "autoconfig.h"
#include "dummy_driver.hpp"

#if defined(CONFIG_RPC_CLIENT)
#include "driver_manager.hpp"
#include "audio_studio/rpc/json_rpc.hpp"
#if defined(CONFIG_RPC_TRANSPORT_PIPE)
#include "audio_studio/rpc/rpc_pipe_transport.hpp"
#endif
#if defined(CONFIG_RPC_TRANSPORT_SOCKET)
#include "audio_studio/rpc/rpc_socket_transport.hpp"
#endif
#endif

namespace audio_studio::cli {
namespace {

#if defined(CONFIG_RPC_CLIENT)
std::string defaultRpcMethod(const std::string& tool, const std::string& action) {
  if (tool == "as_control" && (action.empty() || action == "get-health")) return "server.health";
  if (tool == "as_play") return "audio.createPlaybackSession";
  if (tool == "as_record") return "audio.createCaptureSession";
  if (tool == "as_log") return "server.health";
  if (tool == "as_dump") return "server.health";
  return "server.health";
}

rpc::JsonValue defaultRpcParams(const std::string& method, const Args& args) {
  rpc::JsonValue params = rpc::JsonValue::object();
  if (method == "audio.createPlaybackSession") {
    params["session_id"] = args.valueAfter("--session", "cli-playback");
    params["sample_rate"] = static_cast<uint32_t>(std::stoul(args.valueAfter("--sample-rate", "48000")));
    params["channels"] = static_cast<uint32_t>(std::stoul(args.valueAfter("--channels", "2")));
    params["bytes_per_sample"] = static_cast<uint32_t>(std::stoul(args.valueAfter("--bytes-per-sample", "2")));
  } else if (method == "audio.start" || method == "audio.stop" || method == "audio.closeSession") {
    params["session_id"] = args.valueAfter("--session", "cli-playback");
  }
  return params;
}
#endif

} // namespace

Args::Args(int argc, char** argv) {
  values_.reserve(argc > 0 ? static_cast<size_t>(argc - 1) : 0);
  for (int i = 1; i < argc; ++i) values_.push_back(argv[i]);
}

Args::Args(std::vector<std::string> values) : values_(std::move(values)) {}

bool Args::has(const std::string& flag) const {
  for (const auto& value : values_) {
    if (value == flag) return true;
  }
  return false;
}

std::string Args::valueAfter(const std::string& flag, const std::string& fallback) const {
  for (size_t i = 0; i + 1 < values_.size(); ++i) {
    if (values_[i] == flag) return values_[i + 1];
  }
  return fallback;
}

const std::vector<std::string>& Args::values() const {
  return values_;
}

std::string jsonEscape(const std::string& input) {
  std::string out;
  out.reserve(input.size() + 8);
  for (const char c : input) {
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else out.push_back(c);
  }
  return out;
}

std::string okJson(const std::string& tool, const std::string& detail) {
  std::ostringstream out;
  out << "{\"ok\":true,\"tool\":\"" << jsonEscape(tool)
      << "\",\"detail\":\"" << jsonEscape(detail) << "\"}";
  return out.str();
}

std::string usageText(const std::string& tool, const std::string& action) {
  return "usage: " + tool + " [--self-test|--target dummy] (" + action + ")";
}

int runDummyTool(const std::string& tool, const std::string& action, const Args& args) {
  if (args.has("--help")) {
    std::cout << usageText(tool, action) << "\n";
    return 0;
  }
  const std::string target = args.valueAfter("--target", "dummy");
  if (target != "dummy") {
    std::cerr << "{\"ok\":false,\"error\":\"only dummy target is available in host-alone mode\"}\n";
    return 2;
  }

  audio_studio::drivers::dummy::DummyDriver driver;
  auto status = driver.open();
  if (!status.ok()) {
    std::cerr << status.toJson() << "\n";
    return 1;
  }
  driver.start();
  driver.sendCommand(tool + ":" + action);
  driver.stop();

  std::cout << okJson(tool, action) << "\n";
  return 0;
}

int runCliTool(const std::string& tool, const std::string& action, const Args& args) {
  if (!args.has("--rpc")) return runDummyTool(tool, action, args);

#if !defined(CONFIG_RPC_CLIENT)
  (void)tool;
  (void)action;
  (void)args;
  std::cerr << "{\"ok\":false,\"error\":\"CLI was built without CONFIG_RPC_CLIENT\"}\n";
  return 2;
#else
  const std::string transport_name = args.valueAfter("--rpc");
  auto& manager = drivers::DriverManager::instance();
  auto status = manager.initialize();
  if (!status.ok()) {
    std::cerr << status.toJson() << "\n";
    return 1;
  }

  try {
    const std::string method = args.valueAfter("--method", defaultRpcMethod(tool, action));
    const rpc::JsonValue params = defaultRpcParams(method, args);

    if (transport_name == "socket") {
#if defined(CONFIG_RPC_TRANSPORT_SOCKET)
      const auto port = static_cast<uint16_t>(std::stoul(args.valueAfter("--port", "9900")));
      rpc::SocketJsonRpcTransport transport(manager.socket(), {args.valueAfter("--host", "127.0.0.1"), port, 5000});
      rpc::JsonRpcClient client(transport);
      std::cout << client.call(method, params).dump() << "\n";
      manager.shutdown();
      return 0;
#else
      throw rpc::JsonRpcError(rpc::JsonRpcErrorCode::kInvalidParams, "socket RPC transport is not enabled");
#endif
    }

    if (transport_name == "pipe") {
#if defined(CONFIG_RPC_TRANSPORT_PIPE)
      rpc::PipeJsonRpcTransport transport(manager.pipe(), {args.valueAfter("--request-pipe"), args.valueAfter("--response-pipe"), 5000});
      rpc::JsonRpcClient client(transport);
      std::cout << client.call(method, params).dump() << "\n";
      manager.shutdown();
      return 0;
#else
      throw rpc::JsonRpcError(rpc::JsonRpcErrorCode::kInvalidParams, "pipe RPC transport is not enabled");
#endif
    }

    std::cerr << "{\"ok\":false,\"error\":\"unsupported RPC transport: " << jsonEscape(transport_name) << "\"}\n";
    manager.shutdown();
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "{\"ok\":false,\"error\":\"" << jsonEscape(error.what()) << "\"}\n";
    manager.shutdown();
    return 1;
  }
#endif
}

} // namespace audio_studio::cli
