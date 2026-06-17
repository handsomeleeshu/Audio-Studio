#include "cli_common.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <CLI/CLI.hpp>

#include "autoconfig.h"
#include "dummy_driver.hpp"

#if defined(CONFIG_RPC_CLIENT)
#include "driver_manager.hpp"
#include "audio_rpc_client.hpp"
#include "json_rpc.hpp"
#include "rpc_api_registry.hpp"
#if defined(CONFIG_RPC_TRANSPORT_PIPE)
#include "rpc_pipe_transport.hpp"
#endif
#if defined(CONFIG_RPC_TRANSPORT_SOCKET)
#include "rpc_socket_transport.hpp"
#endif
#endif

namespace audio_studio::cli {
namespace {

struct CliOptions {
  bool self_test = false;
  std::string target;
  std::string action;
  std::string probe;
  std::string mode;
  std::string rpc = "socket";
  std::string host = "127.0.0.1";
  uint16_t port = 9900;
  std::string request_pipe;
  std::string response_pipe;
  std::string method;
  std::string session;
  uint32_t sample_rate = 48000;
  uint16_t channels = 2;
  uint16_t bytes_per_sample = 2;
  std::string sample_format = "s16le";
  std::string device = "default";
  std::string driver_factory = "linux-host";
  std::string file;
  std::string output;
  uint32_t duration_ms = 1000;
  double seconds = 0.0;
  uint32_t chunk_bytes = 0;
  std::vector<std::string> paths;
};

void addIfSet(std::vector<std::string>& values, const std::string& flag, const std::string& value) {
  if (value.empty()) return;
  values.push_back(flag);
  values.push_back(value);
}

void addValue(std::vector<std::string>& values, const std::string& flag, const std::string& value) {
  values.push_back(flag);
  values.push_back(value);
}

Args argsFromOptions(const CliOptions& options) {
  std::vector<std::string> values;
  if (options.self_test) values.push_back("--self-test");
  addIfSet(values, "--target", options.target);
  addIfSet(values, "--action", options.action);
  addIfSet(values, "--probe", options.probe);
  addIfSet(values, "--mode", options.mode);
  addValue(values, "--rpc", options.rpc);
  addValue(values, "--host", options.host);
  addValue(values, "--port", std::to_string(options.port));
  addIfSet(values, "--request-pipe", options.request_pipe);
  addIfSet(values, "--response-pipe", options.response_pipe);
  addIfSet(values, "--method", options.method);
  addIfSet(values, "--session", options.session);
  addValue(values, "--sample-rate", std::to_string(options.sample_rate));
  addValue(values, "--channels", std::to_string(options.channels));
  addValue(values, "--bytes-per-sample", std::to_string(options.bytes_per_sample));
  addValue(values, "--sample-format", options.sample_format);
  addValue(values, "--device", options.device);
  addValue(values, "--driver-factory", options.driver_factory);
  addIfSet(values, "--file", options.file);
  addIfSet(values, "--output", options.output);
  addValue(values, "--duration-ms", std::to_string(options.duration_ms));
  if (options.seconds > 0.0) addValue(values, "--seconds", std::to_string(options.seconds));
  if (options.chunk_bytes > 0) addValue(values, "--chunk-bytes", std::to_string(options.chunk_bytes));
  values.insert(values.end(), options.paths.begin(), options.paths.end());
  return Args(std::move(values));
}

int parseCliOptions(const std::string& tool, const std::string& default_action, int argc, char** argv, CliOptions& options) {
  options.action = default_action;
  CLI::App app{"Audio Studio command line tool", tool};
  app.option_defaults()->always_capture_default();
  app.add_flag("--self-test", options.self_test, "Run the host-side self test path");
  app.add_option("--target", options.target, "Host-alone target. Use dummy for local smoke tests");
  app.add_option("--action", options.action, "Logical action for registry-bound tools");
  app.add_option("--probe", options.probe, "Dump/probe action alias");
  app.add_option("--mode", options.mode, "Log mode/action alias");
  app.add_option("--rpc", options.rpc, "RPC transport")->check(CLI::IsMember({"socket", "pipe"}));
  app.add_option("--host", options.host, "RPC socket host");
  app.add_option("--port", options.port, "RPC socket port");
  app.add_option("--request-pipe", options.request_pipe, "RPC request FIFO path");
  app.add_option("--response-pipe", options.response_pipe, "RPC response FIFO path");
  app.add_option("--method", options.method, "Override JSON-RPC method for simple control tools");
  app.add_option("--session", options.session, "Explicit session id");
  app.add_option("--sample-rate", options.sample_rate, "Audio sample rate");
  app.add_option("--channels", options.channels, "Audio channel count");
  app.add_option("--bytes-per-sample", options.bytes_per_sample, "Audio bytes per sample");
  app.add_option("--sample-format", options.sample_format, "Audio sample format");
  app.add_option("--device", options.device, "Audio device name");
  app.add_option("--driver-factory", options.driver_factory, "Driver factory name");
  app.add_option("--file", options.file, "Playback input file");
  app.add_option("--output", options.output, "Record output file");
  app.add_option("--duration-ms", options.duration_ms, "Record duration in milliseconds");
  app.add_option("--seconds", options.seconds, "Record duration in seconds");
  app.add_option("--chunk-bytes", options.chunk_bytes, "Audio stream chunk size in bytes");
  app.add_option("path", options.paths, "Optional playback input or record output path");
  app.allow_extras(false);

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& error) {
    return app.exit(error);
  }

  if (!options.probe.empty() && options.action == default_action) options.action = options.probe;
  if (!options.mode.empty() && options.action == default_action) options.action = options.mode;
  return -1;
}

#if defined(CONFIG_RPC_CLIENT)
constexpr uint32_t kDefaultChunkBytes = 65536;
constexpr uint32_t kDefaultRecordDurationMs = 1000;

std::string defaultRpcMethod(const std::string& tool, const std::string& action) {
  return rpc::audioStudioRpcApiRegistry().defaultMethodForTool(tool, action);
}

rpc::JsonValue defaultRpcParams(const std::string& method, const Args& args) {
  if (args.has("--session")) {
    rpc::JsonValue params = rpc::JsonValue::object();
    params["session_id"] = args.valueAfter("--session");
    return params;
  }
  const auto* spec = rpc::audioStudioRpcApiRegistry().findMethod(method);
  return spec == nullptr ? rpc::JsonValue::object() : spec->params_example;
}

rpc::AudioSessionConfig audioSessionConfigFromArgs(const Args& args) {
  rpc::AudioSessionConfig config;
  config.session_id = args.valueAfter("--session", "");
  config.sample_rate = static_cast<uint32_t>(std::stoul(args.valueAfter("--sample-rate", "48000")));
  config.channels = static_cast<uint16_t>(std::stoul(args.valueAfter("--channels", "2")));
  config.bytes_per_sample = static_cast<uint16_t>(std::stoul(args.valueAfter("--bytes-per-sample", "2")));
  config.sample_format = args.valueAfter("--sample-format", "s16le");
  config.device_name = args.valueAfter("--device", "default");
  config.driver_factory = args.valueAfter("--driver-factory", "linux-host");
  return config;
}

std::string rpcTransportName(const Args& args) {
  std::string transport = args.valueAfter("--rpc", "socket");
  if (transport.empty() || transport.rfind("--", 0) == 0) return "socket";
  return transport;
}

bool takesValue(const std::string& flag) {
  static const std::vector<std::string> flags = {
    "--rpc",
    "--host",
    "--port",
    "--request-pipe",
    "--response-pipe",
    "--session",
    "--sample-rate",
    "--channels",
    "--bytes-per-sample",
    "--sample-format",
    "--device",
    "--driver-factory",
    "--file",
    "--output",
    "--duration-ms",
    "--seconds",
    "--chunk-bytes",
    "--method",
    "--target",
    "--action",
    "--probe",
    "--mode",
  };
  return std::find(flags.begin(), flags.end(), flag) != flags.end();
}

std::string firstPositionalPath(const Args& args) {
  bool skip_next = false;
  for (const auto& value : args.values()) {
    if (skip_next) {
      skip_next = false;
      continue;
    }
    if (value.rfind("--", 0) == 0) {
      skip_next = takesValue(value);
      continue;
    }
    return value;
  }
  return {};
}

std::string playbackPathFromArgs(const Args& args) {
  const std::string file = args.valueAfter("--file");
  return file.empty() ? firstPositionalPath(args) : file;
}

std::string recordPathFromArgs(const Args& args) {
  const std::string output = args.valueAfter("--output");
  return output.empty() ? firstPositionalPath(args) : output;
}

uint32_t uint32Arg(const Args& args, const std::string& flag, uint32_t fallback) {
  const std::string value = args.valueAfter(flag);
  if (value.empty()) return fallback;
  return static_cast<uint32_t>(std::stoul(value));
}

size_t alignDown(size_t value, size_t unit) {
  if (unit == 0) return value;
  return (value / unit) * unit;
}

size_t frameBytes(const rpc::AudioSessionConfig& config) {
  return static_cast<size_t>(config.channels) * static_cast<size_t>(config.bytes_per_sample);
}

size_t chunkBytesFromArgs(const Args& args, const rpc::AudioSessionConfig& config, const rpc::AudioStreamDescriptor& stream) {
  const size_t bytes_per_frame = std::max<size_t>(1, frameBytes(config));
  size_t chunk = uint32Arg(args, "--chunk-bytes", std::min(stream.max_chunk_bytes, kDefaultChunkBytes));
  chunk = alignDown(std::max(chunk, bytes_per_frame), bytes_per_frame);
  return chunk == 0 ? bytes_per_frame : chunk;
}

uint16_t readLe16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t readLe32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

void writeLe16(std::ostream& out, uint16_t value) {
  const char bytes[] = {
    static_cast<char>(value & 0xff),
    static_cast<char>((value >> 8) & 0xff),
  };
  out.write(bytes, sizeof(bytes));
}

void writeLe32(std::ostream& out, uint32_t value) {
  const char bytes[] = {
    static_cast<char>(value & 0xff),
    static_cast<char>((value >> 8) & 0xff),
    static_cast<char>((value >> 16) & 0xff),
    static_cast<char>((value >> 24) & 0xff),
  };
  out.write(bytes, sizeof(bytes));
}

struct WavInfo {
  uint32_t sample_rate = 48000;
  uint16_t channels = 2;
  uint16_t bytes_per_sample = 2;
  uint64_t data_offset = 0;
  uint64_t data_bytes = 0;
};

bool fourccEquals(const std::array<char, 4>& value, const char* expected) {
  return std::memcmp(value.data(), expected, value.size()) == 0;
}

bool seekForward(std::istream& input, uint64_t bytes) {
  input.seekg(static_cast<std::streamoff>(bytes), std::ios::cur);
  return static_cast<bool>(input);
}

bool readWavHeader(std::ifstream& input, WavInfo& info) {
  input.clear();
  input.seekg(0, std::ios::beg);

  std::array<char, 12> riff {};
  input.read(riff.data(), static_cast<std::streamsize>(riff.size()));
  if (input.gcount() != static_cast<std::streamsize>(riff.size())) {
    input.clear();
    input.seekg(0, std::ios::beg);
    return false;
  }
  if (std::memcmp(riff.data(), "RIFF", 4) != 0 || std::memcmp(riff.data() + 8, "WAVE", 4) != 0) {
    input.clear();
    input.seekg(0, std::ios::beg);
    return false;
  }

  bool saw_fmt = false;
  bool saw_data = false;
  while (input) {
    std::array<char, 4> chunk_id {};
    std::array<uint8_t, 4> chunk_size_bytes {};
    input.read(chunk_id.data(), static_cast<std::streamsize>(chunk_id.size()));
    if (input.gcount() == 0) break;
    if (input.gcount() != static_cast<std::streamsize>(chunk_id.size())) break;
    input.read(reinterpret_cast<char*>(chunk_size_bytes.data()), static_cast<std::streamsize>(chunk_size_bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(chunk_size_bytes.size())) break;
    const uint32_t chunk_size = readLe32(chunk_size_bytes.data());

    if (fourccEquals(chunk_id, "fmt ")) {
      if (chunk_size < 16) throw std::runtime_error("WAV fmt chunk is too small");
      std::vector<uint8_t> fmt(chunk_size);
      input.read(reinterpret_cast<char*>(fmt.data()), static_cast<std::streamsize>(fmt.size()));
      if (input.gcount() != static_cast<std::streamsize>(fmt.size())) throw std::runtime_error("failed to read WAV fmt chunk");

      const uint16_t audio_format = readLe16(fmt.data());
      if (audio_format != 1) throw std::runtime_error("only PCM WAV is supported");
      info.channels = readLe16(fmt.data() + 2);
      info.sample_rate = readLe32(fmt.data() + 4);
      const uint16_t bits_per_sample = readLe16(fmt.data() + 14);
      if (bits_per_sample == 0 || bits_per_sample % 8 != 0) throw std::runtime_error("unsupported WAV sample width");
      info.bytes_per_sample = static_cast<uint16_t>(bits_per_sample / 8);
      saw_fmt = true;
    } else if (fourccEquals(chunk_id, "data")) {
      info.data_offset = static_cast<uint64_t>(input.tellg());
      info.data_bytes = chunk_size;
      if (!seekForward(input, chunk_size)) throw std::runtime_error("failed to seek WAV data chunk");
      saw_data = true;
    } else {
      if (!seekForward(input, chunk_size)) throw std::runtime_error("failed to seek WAV chunk");
    }

    if (chunk_size % 2 != 0) {
      if (!seekForward(input, 1)) throw std::runtime_error("failed to seek WAV padding");
    }
  }

  if (!saw_fmt || !saw_data) throw std::runtime_error("WAV file missing fmt or data chunk");
  input.clear();
  input.seekg(static_cast<std::streamoff>(info.data_offset), std::ios::beg);
  return true;
}

uint32_t boundedU32(uint64_t value, const std::string& field) {
  if (value > std::numeric_limits<uint32_t>::max()) {
    throw std::runtime_error(field + " exceeds WAV 32-bit size limit");
  }
  return static_cast<uint32_t>(value);
}

void writeWavHeader(std::ostream& out, const rpc::AudioSessionConfig& config, uint64_t data_bytes) {
  const uint32_t data_size = boundedU32(data_bytes, "WAV data");
  const uint32_t riff_size = boundedU32(36ULL + data_size, "WAV RIFF");
  const uint16_t block_align = static_cast<uint16_t>(config.channels * config.bytes_per_sample);
  const uint32_t byte_rate = config.sample_rate * block_align;

  out.write("RIFF", 4);
  writeLe32(out, riff_size);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  writeLe32(out, 16);
  writeLe16(out, 1);
  writeLe16(out, config.channels);
  writeLe32(out, config.sample_rate);
  writeLe32(out, byte_rate);
  writeLe16(out, block_align);
  writeLe16(out, static_cast<uint16_t>(config.bytes_per_sample * 8));
  out.write("data", 4);
  writeLe32(out, data_size);
}

void patchWavHeader(const std::string& path, const rpc::AudioSessionConfig& config, uint64_t data_bytes) {
  std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
  if (!file) throw std::runtime_error("failed to reopen WAV output for header patch: " + path);
  writeWavHeader(file, config, data_bytes);
}

void applyWavInfo(rpc::AudioSessionConfig& config, const WavInfo& wav) {
  config.sample_rate = wav.sample_rate;
  config.channels = wav.channels;
  config.bytes_per_sample = wav.bytes_per_sample;
  switch (config.bytes_per_sample) {
    case 1:
      config.sample_format = "u8";
      break;
    case 2:
      config.sample_format = "s16le";
      break;
    case 3:
      config.sample_format = "s24le";
      break;
    case 4:
      config.sample_format = "s32le";
      break;
    default:
      config.sample_format = "pcm";
      break;
  }
}

int runPlayback(rpc::AudioRpcClient& audio, const Args& args) {
  const std::string path = playbackPathFromArgs(args);
  auto config = audioSessionConfigFromArgs(args);
  if (path.empty()) {
    auto playback = audio.createPlaybackSession(config);
    std::cout << "{\"ok\":true,\"tool\":\"as_play\",\"session_id\":\""
              << jsonEscape(playback.sessionId()) << "\",\"stream_uri\":\""
              << jsonEscape(playback.stream().uri) << "\"}\n";
    return 0;
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("failed to open playback input: " + path);

  WavInfo wav;
  bool bounded_input = false;
  uint64_t bytes_remaining = 0;
  if (readWavHeader(input, wav)) {
    applyWavInfo(config, wav);
    bounded_input = true;
    bytes_remaining = wav.data_bytes;
  }

  auto playback = audio.createPlaybackSession(config);
  uint64_t bytes_written = 0;
  try {
    playback.start();
    const size_t chunk_bytes = chunkBytesFromArgs(args, config, playback.stream());
    const size_t bytes_per_frame = std::max<size_t>(1, frameBytes(config));
    std::vector<uint8_t> buffer(chunk_bytes);
    while (input && (!bounded_input || bytes_remaining > 0)) {
      size_t to_read = chunk_bytes;
      if (bounded_input) to_read = static_cast<size_t>(std::min<uint64_t>(bytes_remaining, chunk_bytes));
      input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(to_read));
      size_t got = static_cast<size_t>(input.gcount());
      if (got == 0) break;
      if (bounded_input) bytes_remaining -= got;

      got = alignDown(got, bytes_per_frame);
      if (got == 0) break;
      std::vector<uint8_t> frame(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(got));
      auto written = playback.writeFrames(frame);
      bytes_written += written.bytes;
    }
    playback.drain();
    playback.stop();
    playback.close();
  } catch (...) {
    try {
      playback.stop();
      playback.close();
    } catch (...) {
    }
    throw;
  }

  std::cout << "{\"ok\":true,\"tool\":\"as_play\",\"file\":\"" << jsonEscape(path)
            << "\",\"session_id\":\"" << jsonEscape(playback.sessionId())
            << "\",\"stream_uri\":\"" << jsonEscape(playback.stream().uri)
            << "\",\"played_bytes\":" << bytes_written << "}\n";
  return 0;
}

uint32_t recordDurationMsFromArgs(const Args& args) {
  const std::string seconds = args.valueAfter("--seconds");
  if (!seconds.empty()) return static_cast<uint32_t>(std::stod(seconds) * 1000.0);
  return uint32Arg(args, "--duration-ms", kDefaultRecordDurationMs);
}

int runRecord(rpc::AudioRpcClient& audio, const Args& args) {
  const std::string path = recordPathFromArgs(args);
  auto config = audioSessionConfigFromArgs(args);
  if (path.empty()) {
    auto capture = audio.createCaptureSession(config);
    std::cout << "{\"ok\":true,\"tool\":\"as_record\",\"session_id\":\""
              << jsonEscape(capture.sessionId()) << "\",\"stream_uri\":\""
              << jsonEscape(capture.stream().uri) << "\"}\n";
    return 0;
  }

  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) throw std::runtime_error("failed to open record output: " + path);
  writeWavHeader(output, config, 0);

  auto capture = audio.createCaptureSession(config);
  uint64_t bytes_recorded = 0;
  try {
    capture.start();
    const size_t bytes_per_frame = std::max<size_t>(1, frameBytes(config));
    const size_t chunk_bytes = chunkBytesFromArgs(args, config, capture.stream());
    const uint64_t target_bytes = alignDown(
      static_cast<uint64_t>(recordDurationMsFromArgs(args)) * config.sample_rate * bytes_per_frame / 1000,
      bytes_per_frame);

    while (bytes_recorded < target_bytes) {
      const size_t remaining = static_cast<size_t>(std::min<uint64_t>(target_bytes - bytes_recorded, chunk_bytes));
      const size_t to_read = alignDown(std::max(remaining, bytes_per_frame), bytes_per_frame);
      auto result = capture.readFrames(to_read);
      if (!result.ok || result.data.empty()) break;
      if (bytes_recorded + result.data.size() > target_bytes) {
        result.data.resize(static_cast<size_t>(target_bytes - bytes_recorded));
      }
      output.write(reinterpret_cast<const char*>(result.data.data()), static_cast<std::streamsize>(result.data.size()));
      bytes_recorded += result.data.size();
    }
    capture.stop();
    capture.close();
  } catch (...) {
    try {
      capture.stop();
      capture.close();
    } catch (...) {
    }
    throw;
  }

  output.close();
  patchWavHeader(path, config, bytes_recorded);

  std::cout << "{\"ok\":true,\"tool\":\"as_record\",\"file\":\"" << jsonEscape(path)
            << "\",\"session_id\":\"" << jsonEscape(capture.sessionId())
            << "\",\"stream_uri\":\"" << jsonEscape(capture.stream().uri)
            << "\",\"recorded_bytes\":" << bytes_recorded << "}\n";
  return 0;
}

int runAudioTool(const std::string& tool, rpc::JsonRpcClient& client, rpc::IRpcStreamTransport* stream_transport, const Args& args) {
  rpc::AudioRpcClient audio(client, stream_transport);
  if (tool == "as_play") {
    return runPlayback(audio, args);
  }
  if (tool == "as_record") {
    return runRecord(audio, args);
  }
  return -1;
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
  if (args.has("--self-test") || args.valueAfter("--target") == "dummy") return runDummyTool(tool, action, args);

#if !defined(CONFIG_RPC_CLIENT)
  (void)tool;
  (void)action;
  (void)args;
  std::cerr << "{\"ok\":false,\"error\":\"CLI was built without CONFIG_RPC_CLIENT\"}\n";
  return 2;
#else
  const std::string transport_name = rpcTransportName(args);
  auto& manager = drivers::DriverManager::instance();
  drivers::DriverManagerConfig driver_config;
  driver_config.enable_os = false;
  driver_config.enable_socket = transport_name == "socket";
  driver_config.enable_filesystem = false;
  driver_config.enable_pipe = transport_name == "pipe";
  driver_config.enable_dynlib = false;
  driver_config.enable_transport = false;
  driver_config.enable_audio = false;
  driver_config.enable_control = false;
  driver_config.enable_log = false;
  driver_config.enable_dump = false;
  auto status = manager.initialize(driver_config);
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
      rpc::SocketRpcStreamTransport stream_transport(manager.socket(), {args.valueAfter("--host", "127.0.0.1"), port, 5000});
      rpc::JsonRpcClient client(transport);
      const int audio_result = runAudioTool(tool, client, &stream_transport, args);
      if (audio_result >= 0) {
        manager.shutdown();
        return audio_result;
      }
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
      rpc::PipeRpcStreamTransport stream_transport(manager.pipe(), {args.valueAfter("--request-pipe"), args.valueAfter("--response-pipe"), 5000});
      rpc::JsonRpcClient client(transport);
      const int audio_result = runAudioTool(tool, client, &stream_transport, args);
      if (audio_result >= 0) {
        manager.shutdown();
        return audio_result;
      }
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

int runCliTool(const std::string& tool, const std::string& default_action, int argc, char** argv) {
  CliOptions options;
  const int parse_result = parseCliOptions(tool, default_action, argc, argv, options);
  if (parse_result >= 0) return parse_result;
  return runCliTool(tool, options.action, argsFromOptions(options));
}

} // namespace audio_studio::cli
