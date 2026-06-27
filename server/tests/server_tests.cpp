#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

#include "autoconfig.h"
#include "audio_service.hpp"
#include "datalink_frame.hpp"
#include "datalink_manager.hpp"
#include "frame_codec.hpp"
#include "service_registry.hpp"
#include "status.hpp"
#include "log_service.hpp"
#include "system_info_service.hpp"
#include "transport_manager.hpp"
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

class ScriptedDataLinkDevice final : public audio_studio::drivers::datalink::IDataLinkDevice {
public:
  explicit ScriptedDataLinkDevice(size_t mtu) : mtu_(mtu) {}

  audio_studio::framework::Status open(const audio_studio::drivers::datalink::DataLinkDeviceConfig& config) override {
    name_ = config.name.empty() ? "scripted" : config.name;
    connected_ = true;
    return audio_studio::framework::Status::success();
  }

  void close() override { connected_ = false; }

  audio_studio::framework::Status writeBlock(const uint8_t* data, size_t size, uint32_t) override {
    if (!connected_) return audio_studio::framework::Status::unavailable("scripted datalink is closed");
    if (data == nullptr && size > 0) return audio_studio::framework::Status::invalidArgument("scripted datalink write is null");
    audio_studio::framework::transport::DataLinkFrame frame;
    auto status = audio_studio::framework::transport::DataLinkFrameCodec::decode(data, size, frame);
    if (!status.ok()) return status;
    if ((frame.flags & audio_studio::framework::transport::kDataLinkFrameData) != 0) {
      ++data_frames_;
      std::vector<uint8_t> transport_payload = frame.payload;
      auto ack = audio_studio::framework::transport::DataLinkFrameCodec::makeAck(frame);
      if (!sent_nak_ && inject_first_nak_) {
        ack.flags = audio_studio::framework::transport::kDataLinkFrameNak;
        sent_nak_ = true;
      }
      auto encoded = audio_studio::framework::transport::DataLinkFrameCodec::encode(ack);
      rx_.insert(rx_.end(), encoded.begin(), encoded.end());
      if (auto_transport_ack_) {
        audio_studio::framework::transport::TransportFrame request;
        if (audio_studio::framework::transport::FrameCodec::decode(transport_payload.data(), transport_payload.size(), request).ok() &&
            ((request.flags & audio_studio::framework::transport::kTransportFrameAck) == 0)) {
          audio_studio::framework::transport::TransportFrame response;
          response.version = request.version;
          response.channel_id = request.channel_id;
          response.command_id = request.command_id;
          response.flags = audio_studio::framework::transport::kTransportFrameAck |
                           audio_studio::framework::transport::kTransportFrameResponse;
          response.sequence_id = request.sequence_id;
          response.session_id = request.session_id;
          response.payload = {'o', 'k'};
          enqueueTransportPacket(response);
        }
      }
    }
    written_.insert(written_.end(), data, data + size);
    return audio_studio::framework::Status::success();
  }

  audio_studio::framework::Status readBlock(uint8_t* buffer, size_t capacity, size_t& actual_size, uint32_t) override {
    actual_size = 0;
    if (!connected_) return audio_studio::framework::Status::unavailable("scripted datalink is closed");
    if (buffer == nullptr && capacity > 0) return audio_studio::framework::Status::invalidArgument("scripted datalink read is null");
    const size_t count = std::min(capacity, rx_.size());
    if (count == 0) return audio_studio::framework::Status::unavailable("scripted datalink has no frame");
    std::copy(rx_.begin(), rx_.begin() + static_cast<long>(count), buffer);
    rx_.erase(rx_.begin(), rx_.begin() + static_cast<long>(count));
    actual_size = count;
    return audio_studio::framework::Status::success();
  }

  audio_studio::framework::Status flush() override { return audio_studio::framework::Status::success(); }
  bool isConnected() const override { return connected_; }
  audio_studio::drivers::datalink::DataLinkDeviceCaps caps() const override {
    return {mtu_, false, true};
  }
  std::string name() const override { return name_; }

  void injectFirstNak() { inject_first_nak_ = true; }
  void enableTransportAck() { auto_transport_ack_ = true; }
  size_t dataFrames() const { return data_frames_; }
  const std::vector<uint8_t>& writtenBytes() const { return written_; }

private:
  void enqueueTransportPacket(const audio_studio::framework::transport::TransportFrame& transport_frame) {
    using namespace audio_studio::framework::transport;
    const auto payload = FrameCodec::encode(transport_frame);
    DataLinkFrame frame;
    frame.flags = kDataLinkFrameData | kDataLinkFrameEnd;
    frame.link_sequence = next_peer_sequence_++;
    frame.transport_size = static_cast<uint32_t>(payload.size());
    frame.fragment_offset = 0;
    frame.fragment_index = 0;
    frame.fragment_count = 1;
    frame.payload = payload;
    const auto encoded = DataLinkFrameCodec::encode(frame);
    rx_.insert(rx_.end(), encoded.begin(), encoded.end());
  }

  size_t mtu_ = 64;
  bool connected_ = false;
  bool inject_first_nak_ = false;
  bool sent_nak_ = false;
  bool auto_transport_ack_ = false;
  size_t data_frames_ = 0;
  uint32_t next_peer_sequence_ = 1000;
  std::string name_;
  std::vector<uint8_t> rx_;
  std::vector<uint8_t> written_;
};

class ScriptedLogDevice final : public audio_studio::drivers::log::ILogDevice {
public:
  explicit ScriptedLogDevice(std::vector<std::vector<uint8_t>> chunks) : chunks_(std::move(chunks)) {}

  audio_studio::framework::Status open(const audio_studio::drivers::log::LogDeviceConfig& config) override {
    config_ = config;
    open_ = true;
    return audio_studio::framework::Status::success();
  }

  audio_studio::framework::Status configure(const audio_studio::drivers::log::LogDeviceConfig& config) override {
    if (!open_) return audio_studio::framework::Status::unavailable("scripted log device is not open");
    config_ = config;
    configured_options_ = config.options;
    return audio_studio::framework::Status::success();
  }

  audio_studio::framework::Status start() override {
    if (!open_) return audio_studio::framework::Status::unavailable("scripted log device is not open");
    running_ = true;
    return audio_studio::framework::Status::success();
  }

  audio_studio::framework::Status stop() override {
    running_ = false;
    return audio_studio::framework::Status::success();
  }

  audio_studio::framework::Status readChunk(audio_studio::drivers::log::LogRawChunk& chunk, uint32_t) override {
    if (!running_) return audio_studio::framework::Status::unavailable("scripted log device is not running");
    if (read_index_ >= chunks_.size()) return audio_studio::framework::Status::unavailable("scripted log device has no chunk");
    chunk.sequence = static_cast<uint32_t>(read_index_ + 1);
    chunk.bytes = chunks_[read_index_++];
    return audio_studio::framework::Status::success();
  }

  audio_studio::framework::Status getStats(audio_studio::drivers::log::LogDeviceStats& stats) override {
    stats = {chunks_.size(), read_index_, running_};
    return audio_studio::framework::Status::success();
  }

  void close() override {
    open_ = false;
    running_ = false;
  }

  const std::map<std::string, std::string>& configuredOptions() const { return configured_options_; }

private:
  audio_studio::drivers::log::LogDeviceConfig config_;
  bool open_ = false;
  bool running_ = false;
  size_t read_index_ = 0;
  std::vector<std::vector<uint8_t>> chunks_;
  std::map<std::string, std::string> configured_options_;
};

class ScriptedLogDeviceFactory final : public audio_studio::drivers::log::ILogDeviceFactory {
public:
  explicit ScriptedLogDeviceFactory(std::vector<std::vector<uint8_t>> chunks) : chunks_(std::move(chunks)) {}

  std::string name() const override { return "scripted-log"; }

  std::unique_ptr<audio_studio::drivers::log::ILogDevice> create(const audio_studio::drivers::log::LogDeviceConfig& config) const override {
    auto device = std::make_unique<ScriptedLogDevice>(chunks_);
    if (!device->open(config).ok()) return nullptr;
    last_device_ = device.get();
    ++created_count_;
    return device;
  }

  ScriptedLogDevice* lastDevice() const { return last_device_; }
  size_t createdCount() const { return created_count_; }

private:
  std::vector<std::vector<uint8_t>> chunks_;
  mutable ScriptedLogDevice* last_device_ = nullptr;
  mutable size_t created_count_ = 0;
};

std::string tempPath(const std::string& name) {
  return "/tmp/audio-studio-log-service-test-" + std::to_string(static_cast<long long>(getpid())) + "-" + name;
}

bool pathExists(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  return input.good();
}

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

#if defined(CONFIG_FRAMEWORK_CONFIG)
bool jsonContainsStringValue(const audio_studio::rpc::JsonValue& value, const std::string& needle) {
  if (value.isString()) return value.asString() == needle;
  if (value.isArray()) {
    for (const auto& item : value.asArray()) {
      if (jsonContainsStringValue(item, needle)) return true;
    }
  }
  if (value.isObject()) {
    for (const auto& item : value.asObject()) {
      if (jsonContainsStringValue(item.second, needle)) return true;
    }
  }
  return false;
}

bool hasModuleType(const audio_studio::rpc::JsonValue& root, const std::string& type_id) {
  for (const auto& item : root.at("module_types").asArray()) {
    if (item.at("type_id").asString() == type_id) return true;
  }
  return false;
}

bool hasPreset(const audio_studio::rpc::JsonValue& root,
               const std::string& preset_id,
               const std::string& load_mode) {
  for (const auto& item : root.at("presets").asArray()) {
    if (item.at("preset_id").asString() == preset_id && item.at("load_mode").asString() == load_mode) {
      return true;
    }
  }
  return false;
}

bool nodeHasModuleType(const audio_studio::rpc::JsonValue& pipeline,
                       const std::string& node_id,
                       const std::string& module_type) {
  for (const auto& node : pipeline.at("nodes").asArray()) {
    if (node.at("node_id").asString() == node_id &&
        node.at("module_type").asString() == module_type) {
      return true;
    }
  }
  return false;
}

void assertInlineEndpointSchema(const std::string& path) {
  const std::string text = readFileText(path);
  assert(text.find("inspecrot") == std::string::npos);
  assert(text.find("inspetpr") == std::string::npos);
  assert(text.find("\"RUNTIME\"") == std::string::npos);
  assert(text.find("virtual.file_input") == std::string::npos);
  assert(text.find("virtual.audio_output") == std::string::npos);
  assert(text.find("\"naming\"") == std::string::npos);

  const auto root = audio_studio::rpc::parseJson(text);
  assert(!jsonContainsStringValue(root, "RUNTIME"));
  assert(!root.has("module_instances"));
  assert(root.has("pipelines"));
  assert(root.has("frontend_connections"));
  assert(hasPreset(root, "inspector_preset", "inspector"));

  if (root.has("resource_catalog")) {
    assert(!root.at("resource_catalog").has("audio_endpoints"));
  }

  for (const auto& pipeline : root.at("pipelines").asArray()) {
    assert(!pipeline.has("ports"));
    assert(pipeline.has("frame"));
    assert(!pipeline.at("frame").has("rate"));
    for (const auto& node : pipeline.at("nodes").asArray()) {
      assert(!node.has("kind"));
      assert(!node.has("inst_ref"));
      assert(node.has("module_type"));
      assert(node.has("params"));
    }
  }
  assert(nodeHasModuleType(root.at("pipelines").asArray().front(), "HOST_IN", "builtin.host"));
  assert(nodeHasModuleType(root.at("pipelines").asArray().front(), "DAI_OUT", "builtin.dai"));
}
#endif

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

  {
    using namespace audio_studio::framework::transport;
    DataLinkFrame frame;
    frame.flags = kDataLinkFrameData | kDataLinkFrameEnd;
    frame.link_sequence = 42;
    frame.transport_size = 5;
    frame.fragment_offset = 0;
    frame.fragment_index = 0;
    frame.fragment_count = 1;
    frame.payload = {1, 2, 3, 4, 5};
    const auto encoded = DataLinkFrameCodec::encode(frame);
    DataLinkFrame decoded;
    assert(DataLinkFrameCodec::decode(encoded.data(), encoded.size(), decoded).ok());
    assert(decoded.link_sequence == 42);
    assert(decoded.payload == frame.payload);
    auto corrupted = encoded;
    corrupted.back() ^= 0xff;
    assert(!DataLinkFrameCodec::decode(corrupted.data(), corrupted.size(), decoded).ok());
  }

  {
    ScriptedDataLinkDevice device(48);
    assert(device.open({"unit-test-link"}).ok());
    device.injectFirstNak();
    audio_studio::framework::transport::DataLinkManager link(device, {5, 2});
    const std::vector<uint8_t> payload(180, 0x7b);
    auto link_status = link.sendPacket(payload, 100);
    if (!link_status.ok()) std::cerr << "sendPacket failed: " << link_status.message() << "\n";
    assert(link_status.ok());
    assert(device.dataFrames() > 4);
    assert(link.stats().retries == 1);
  }

  {
    using namespace audio_studio::framework::transport;
    TransportFrame request;
    request.channel_id = 6;
    request.command_id = 9;
    request.flags = kTransportFrameRequest | kTransportFrameAckRequired;
    request.sequence_id = 77;
    request.session_id = 88;
    request.payload = {0x10, 0x20};
    const auto encoded = FrameCodec::encode(request);
    TransportFrame decoded;
    assert(FrameCodec::decode(encoded.data(), encoded.size(), decoded).ok());
    assert(decoded.command_id == 9);
    assert(decoded.flags == (kTransportFrameRequest | kTransportFrameAckRequired));
    assert(decoded.session_id == 88);
    assert(decoded.payload == request.payload);
    auto corrupted = encoded;
    corrupted.back() ^= 0x5a;
    assert(!FrameCodec::decode(corrupted.data(), corrupted.size(), decoded).ok());
  }

  {
    using namespace audio_studio::framework::transport;
    ScriptedDataLinkDevice device(256);
    assert(device.open({"transport-sync"}).ok());
    device.enableTransportAck();
    auto& manager = TransportManager::instance();
    manager.resetForTesting();
    assert(&manager == &TransportManager::instance());
    assert(manager.bindDataLinkDevice(device).ok());
    assert(manager.openChannel(6, "log").ok());
    assert(!manager.openChannel(6, "audio-data").ok());
    assert(manager.openChannel(3, "audio-control").ok());
    assert(!manager.openChannel(3, "audio-control").ok());
    assert(!manager.openChannel(3, "audio-data").ok());
    assert(manager.closeChannel(3).ok());
    assert(manager.openChannel(3, "audio-control").ok());
    assert(manager.closeChannel(3).ok());
    ScriptedDataLinkDevice second_device(256);
    assert(second_device.open({"transport-second"}).ok());
    assert(!manager.bindDataLinkDevice(second_device).ok());
    TransportFrame response;
    auto sync_status = manager.sendSync(6, 1, {0x01, 0x02}, response, 100);
    if (!sync_status.ok()) std::cerr << "sendSync failed: " << sync_status.message() << "\n";
    assert(sync_status.ok());
    assert((response.flags & kTransportFrameAck) != 0);
    assert(response.payload == std::vector<uint8_t>({'o', 'k'}));

    std::mutex callback_mutex;
    bool callback_called = false;
    assert(manager.sendAsync(6, 2, {0x03}, [&](const audio_studio::framework::Status& status, const TransportFrame& async_response) {
      std::lock_guard<std::mutex> lock(callback_mutex);
      assert(status.ok());
      assert((async_response.flags & kTransportFrameAck) != 0);
      callback_called = true;
    }, 100).ok());
    assert(manager.drainAsync(6).ok());
    {
      std::lock_guard<std::mutex> lock(callback_mutex);
      assert(callback_called);
    }
    assert(manager.closeChannel(6).ok());
    manager.resetForTesting();
  }

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
  audio_studio::framework::log::LogService log_service;
  JsonRpcEndpoint audio_endpoint;
  auto audio_context = std::make_shared<audio_studio::rpc::RpcRuntimeContext>(audio_service);
  audio_context->setLogService(&log_service);
  audio_studio::rpc::registerAudioStudioRpcMethods(audio_endpoint, audio_context);
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
  log_service.configureDeviceRegistry(&drivers.logRegistry());

  {
    using namespace audio_studio;
    framework::system_info::SystemInfoService system_info;
    assert(system_info.consumeLogEntry({1, "info", "SOF",
      "SOF 0 INFO ASINFO|heartbeat|seq=7|timestamp_ms=123400", ""}));
    assert(system_info.consumeLogEntry({2, "info", "SOF",
      "SOF 0 INFO ASINFO|core|id=0|freq_mhz=600|load_percent=37.5", ""}));
    assert(system_info.consumeLogEntry({3, "info", "SOF",
      "SOF 0 INFO ASINFO|module|id=eq_1|pipeline=10|state=ACTIVE|core=0|cpu_percent=12.5|memory_bytes=4096|latency_ms=0.7", ""}));
    assert(system_info.consumeLogEntry({4, "info", "SOF",
      "SOF 0 INFO ASINFO|buffer|id=eq_1.out->sink.in|from=eq_1.out|to=sink.in|size_bytes=8192|avail_bytes=2048|produced_bytes=4800|consumed_bytes=4700", ""}));
    assert(system_info.consumeLogEntry({5, "info", "SOF",
      "SOF 0 INFO ASINFO|heap|category=system_runtime|index=0|block_size=256|free_count=5|total_count=16|used_bytes=2816|free_bytes=1280", ""}));
    assert(system_info.consumeLogEntry({6, "info", "SOF",
      "SOF 0 INFO ASINFO|heap|category=runtime|index=0|block_size=512|free_count=3", ""}));
    assert(system_info.consumeLogEntry({7, "info", "SOF",
      "SOF 0 INFO ASINFO|heap|category=runtime|index=0|block_size=512|total_count=8", ""}));
    assert(system_info.consumeLogEntry({8, "info", "SOF",
      "SOF 0 INFO ASINFO|heap|category=runtime|index=0|block_size=512|used_bytes=2560", ""}));
    assert(system_info.consumeLogEntry({9, "info", "SOF",
      "SOF 0 INFO ASINFO|heap|category=runtime|index=0|block_size=512|free_bytes=1536", ""}));
    auto snapshot = system_info.snapshot();
    assert(snapshot.connected);
    assert(snapshot.cores.size() == 1);
    assert(snapshot.cores.front().load_percent == 37.5);
    assert(snapshot.modules.size() == 1);
    assert(snapshot.modules.front().node_id == "eq_1");
    assert(snapshot.modules.front().state == "ACTIVE");
    assert(snapshot.buffers.size() == 1);
    assert(snapshot.buffers.front().consumed_bytes == 4700);
    assert(snapshot.heap.size() == 2);
    assert(snapshot.heap.front().free_count == 5);
    auto runtime_heap = std::find_if(snapshot.heap.begin(), snapshot.heap.end(),
                                     [](const auto& heap) {
                                       return heap.category == "runtime" && heap.block_size == 512;
                                     });
    assert(runtime_heap != snapshot.heap.end());
    assert(runtime_heap->free_count == 3);
    assert(runtime_heap->total_count == 8);
    assert(runtime_heap->used_bytes == 2560);
    assert(runtime_heap->free_bytes == 1536);
    assert(system_info.consumeLogEntry({10, "info", "SOF",
      "SOF 0 INFO ASINFO|module|id=eq_1|cpu_percent=13.5|memory_bytes=8192|latency_ms=0.8", ""}));
    assert(system_info.consumeLogEntry({11, "info", "SOF",
      "SOF 0 INFO ASINFO|buffer|id=eq_1.out->sink.in|stalled=1", ""}));
    assert(system_info.consumeLogEntry({12, "info", "SOF",
      "SOF 0 INFO ASINFO|module|id=eq_1|state=6", ""}));
    snapshot = system_info.snapshot();
    assert(snapshot.modules.size() == 1);
    assert(snapshot.modules.front().pipeline_id == 10);
    assert(snapshot.modules.front().state == "ACTIVE");
    assert(snapshot.modules.front().memory_bytes == 8192);
    assert(snapshot.buffers.size() == 1);
    assert(snapshot.buffers.front().consumed_bytes == 4700);
    assert(snapshot.buffers.front().stalled);
    assert(system_info.consumeLogEntry({13, "info", "SOF",
      "Suppressed 7 similar messages: ASINFO|module|id=%u|pipeline=%u|state=%d|core=%u", ""}));
    snapshot = system_info.snapshot();
    assert(snapshot.modules.size() == 1);
    assert(snapshot.modules.front().node_id == "eq_1");

    system_info.setHeartbeatTimeoutForTesting(std::chrono::milliseconds(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    auto stale = system_info.snapshot();
    assert(!stale.connected);
    assert(stale.modules.empty());
    assert(stale.buffers.empty());
    assert(stale.heap.empty());

    system_info.setHeartbeatTimeoutForTesting(std::chrono::milliseconds(1000));
    log_service.setEntryInterceptor([&](const framework::log::LogEntry& entry) {
      return system_info.consumeLogEntry(entry);
    });
    assert(log_service.append("info", "ASINFO|heartbeat|seq=8|timestamp_ms=123500").ok());
    assert(log_service.append("info", "ASINFO|module|id=vol_1|pipeline=11|state=READY|core=1|cpu_percent=0.0|memory_bytes=2048|latency_ms=0.0").ok());
    assert(log_service.append("info", "ordinary firmware line").ok());
    auto tail = log_service.tail(8);
    assert(tail.size() == 1);
    assert(tail.front().message == "ordinary firmware line");
    assert(system_info.snapshot().modules.front().node_id == "vol_1");

    audio_studio::framework::log::LogService pump_log_service;
    audio_studio::drivers::log::LogDeviceRegistry pump_registry;
    auto pump_factory = std::make_unique<ScriptedLogDeviceFactory>(
      std::vector<std::vector<uint8_t>>{
        std::vector<uint8_t>{
          'i','n','f','o','|','S','O','F','|','A','S','I','N','F','O','|','h','e','a','r','t','b','e','a','t','|','s','e','q','=','2','1','|','t','i','m','e','s','t','a','m','p','_','m','s','=','1','0','0','\n',
          'i','n','f','o','|','S','O','F','|','A','S','I','N','F','O','|','m','o','d','u','l','e','|','i','d','=','p','u','m','p','_','v','o','l','|','p','i','p','e','l','i','n','e','=','2','|','s','t','a','t','e','=','A','C','T','I','V','E','|','c','o','r','e','=','0','|','c','p','u','_','p','e','r','c','e','n','t','=','4','.','0','|','m','e','m','o','r','y','_','b','y','t','e','s','=','1','0','2','4','|','l','a','t','e','n','c','y','_','m','s','=','0','.','2','\n',
          'i','n','f','o','|','F','W','|','o','r','d','i','n','a','r','y',' ','p','u','m','p',' ','l','o','g','\n'
        }
      });
    auto* pump_factory_ptr = pump_factory.get();
    assert(pump_registry.registerFactory(std::move(pump_factory)).ok());
    pump_log_service.configureDeviceRegistry(&pump_registry);
    framework::system_info::SystemInfoService pump_system_info;
    pump_log_service.setEntryInterceptor([&](const framework::log::LogEntry& entry) {
      return pump_system_info.consumeLogEntry(entry);
    });
    framework::log::LogSessionConfig pump_config;
    pump_config.session_id = "system-info-pump-test";
    pump_config.driver_factory = "scripted-log";
    pump_config.source = "firmware";
    assert(pump_system_info.startLogPump(pump_log_service, pump_config).ok());
    bool pump_updated = false;
    for (int i = 0; i < 20; ++i) {
      if (!pump_system_info.snapshot().modules.empty()) {
        pump_updated = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(pump_updated);
    assert(pump_system_info.snapshot().modules.front().node_id == "pump_vol");
    assert(pump_log_service.tail(8).size() == 1);
    assert(pump_log_service.tail(8).front().message == "ordinary pump log");
    assert(pump_factory_ptr->createdCount() == 1);
    framework::log::LogSessionConfig mirror_config;
    mirror_config.session_id = "as_log";
    mirror_config.driver_factory = "scripted-log";
    mirror_config.source = "firmware";
    framework::log::LogSessionInfo mirror_session;
    assert(pump_log_service.createSession(mirror_config, mirror_session).ok());
    assert(pump_log_service.start(mirror_session.session_id).ok());
    std::vector<framework::log::LogEntry> mirrored_entries;
    assert(pump_log_service.readEntries(mirror_session.session_id, 8, mirrored_entries).ok());
    assert(mirrored_entries.size() == 1);
    assert(mirrored_entries.front().message == "ordinary pump log");
    assert(pump_factory_ptr->createdCount() == 1);
    assert(pump_log_service.closeSession(mirror_session.session_id).ok());
    assert(pump_system_info.stopLogPump().ok());
    assert(!pump_system_info.logPumpRunning());

    audio_studio::framework::log::LogService intercepted_log_service;
    audio_studio::drivers::log::LogDeviceRegistry intercepted_registry;
    assert(intercepted_registry.registerFactory(std::make_unique<ScriptedLogDeviceFactory>(
      std::vector<std::vector<uint8_t>>{
        std::vector<uint8_t>{'i','n','f','o','|','S','O','F','|','A','S','I','N','F','O','|','h','e','a','r','t','b','e','a','t','|','s','e','q','=','3','1','\n'},
        std::vector<uint8_t>{'i','n','f','o','|','S','O','F','|','A','S','I','N','F','O','|','h','e','a','r','t','b','e','a','t','|','s','e','q','=','3','2','\n'}
      })).ok());
    intercepted_log_service.configureDeviceRegistry(&intercepted_registry);
    intercepted_log_service.setEntryInterceptor([](const framework::log::LogEntry& entry) {
      return entry.message.find("ASINFO|") == 0;
    });
    framework::log::LogSessionConfig intercepted_config;
    intercepted_config.session_id = "intercepted-log";
    intercepted_config.driver_factory = "scripted-log";
    intercepted_config.source = "firmware";
    framework::log::LogSessionInfo intercepted_session;
    assert(intercepted_log_service.createSession(intercepted_config, intercepted_session).ok());
    assert(intercepted_log_service.start(intercepted_session.session_id).ok());
    std::vector<framework::log::LogEntry> intercepted_entries;
    assert(intercepted_log_service.readEntries(intercepted_session.session_id, 64, intercepted_entries).ok());
    assert(intercepted_entries.empty());
    framework::log::LogSessionStats intercepted_stats;
    assert(intercepted_log_service.getStats(intercepted_session.session_id, intercepted_stats).ok());
    assert(intercepted_stats.raw_chunks_read == 1);
    assert(intercepted_log_service.closeSession(intercepted_session.session_id).ok());

    audio_context->setSystemInfoService(&system_info);
    auto sys_snapshot = audio_client.call("systemInfo.snapshot");
    assert(sys_snapshot.at("connected").asBool());
    assert(sys_snapshot.at("components").asArray().size() == 1);
    assert(sys_snapshot.at("components").asArray()[0].at("node_id").asString() == "vol_1");
    assert(audio_client.call("systemInfo.components").at("components").asArray().size() == 1);
    assert(audio_client.call("systemInfo.buffers").at("buffers").asArray().empty());
    assert(audio_client.call("systemInfo.health").at("rows").asArray().size() >= 1);

    framework::log::LogSessionConfig config;
    config.session_id = "log-rpc-test";
    config.driver_factory = "linux-host";
    config.source = "firmware";
    config.min_level = "debug";
    framework::log::LogSessionInfo session;
    assert(log_service.createSession(config, session).ok());
    assert(log_service.start(session.session_id).ok());
    std::vector<framework::log::LogEntry> entries;
    assert(log_service.readEntries(session.session_id, 4, entries).ok());
    assert(!entries.empty());
    assert(entries.front().tag == "FW");
    assert(entries.front().message.find("audio controller") != std::string::npos);
    assert(log_service.stop(session.session_id).ok());
    assert(log_service.closeSession(session.session_id).ok());

    {
      audio_studio::framework::log::LogService split_log_service;
      audio_studio::drivers::log::LogDeviceRegistry split_registry;
      assert(split_registry.registerFactory(std::make_unique<ScriptedLogDeviceFactory>(
        std::vector<std::vector<uint8_t>>{
          std::vector<uint8_t>{'i','n','f','o','|','F','W','|','h','e','l'},
          std::vector<uint8_t>{'l','o','\n','w','a','r','n','i','n','g','|','F','W','|','d','o','n','e','\n'}
        })).ok());
      split_log_service.configureDeviceRegistry(&split_registry);
      framework::log::LogSessionConfig split_config;
      split_config.session_id = "split-text-log";
      split_config.driver_factory = "scripted-log";
      split_config.source = "firmware";
      framework::log::LogSessionInfo split_session;
      assert(split_log_service.createSession(split_config, split_session).ok());
      assert(split_log_service.start(split_session.session_id).ok());
      std::vector<framework::log::LogEntry> split_entries;
      assert(split_log_service.readEntries(split_session.session_id, 2, split_entries).ok());
      assert(split_entries.size() == 2);
      assert(split_entries[0].message == "hello");
      assert(split_entries[1].level == "warning");
      assert(split_entries[1].message == "done");
      assert(split_log_service.closeSession(split_session.session_id).ok());
    }

    {
      audio_studio::framework::log::LogService configured_log_service;
      audio_studio::drivers::log::LogDeviceRegistry configured_registry;
      auto factory = std::make_unique<ScriptedLogDeviceFactory>(std::vector<std::vector<uint8_t>>{});
      auto* scripted_factory = factory.get();
      assert(configured_registry.registerFactory(std::move(factory)).ok());
      configured_log_service.configureDeviceRegistry(&configured_registry);
      framework::log::LogSessionConfig defaults;
      defaults.driver_factory = "scripted-log";
      defaults.source = "firmware";
      defaults.options["endpoint"] = "as_datalink";
      defaults.options["trace_ldc"] = "sof.ldc";
      configured_log_service.setDefaultSessionConfig(defaults);
      framework::log::LogSessionConfig configured_config;
      configured_config.session_id = "configured-log";
      configured_config.driver_factory.clear();
      configured_config.source.clear();
      configured_config.min_level.clear();
      framework::log::LogSessionInfo configured_session;
      assert(configured_log_service.createSession(configured_config, configured_session).ok());
      framework::log::LogSessionConfig update;
      update.options["mtu"] = "512";
      assert(configured_log_service.configureSession(configured_session.session_id, update, configured_session).ok());
      assert(scripted_factory->lastDevice());
      const auto& configured_options = scripted_factory->lastDevice()->configuredOptions();
      assert(configured_options.at("endpoint") == "as_datalink");
      assert(configured_options.at("trace_ldc") == "sof.ldc");
      assert(configured_options.at("mtu") == "512");
      assert(configured_log_service.closeSession(configured_session.session_id).ok());
    }

    {
      audio_studio::framework::log::LogService path_log_service;
      framework::log::LogSessionConfig bad_path_config;
      bad_path_config.session_id = "bad-trace-path";
      bad_path_config.options["trace_ldc"] = "sof.ldc";
      bad_path_config.options["raw_trace_path"] = tempPath("missing/raw.trace");
      framework::log::LogSessionInfo bad_path_session;
      assert(!path_log_service.createSession(bad_path_config, bad_path_session).ok());

      const std::string user_raw = tempPath("user.raw");
      const std::string user_decoded = tempPath("user.decoded");
      {
        std::ofstream raw(user_raw, std::ios::binary | std::ios::trunc);
        std::ofstream decoded(user_decoded, std::ios::binary | std::ios::trunc);
        raw << "raw";
        decoded << "decoded";
      }
      framework::log::LogSessionConfig user_path_config;
      user_path_config.session_id = "user-trace-path";
      user_path_config.options["trace_ldc"] = "sof.ldc";
      user_path_config.options["raw_trace_path"] = user_raw;
      user_path_config.options["decoded_trace_path"] = user_decoded;
      framework::log::LogSessionInfo user_path_session;
      assert(path_log_service.createSession(user_path_config, user_path_session).ok());
      assert(path_log_service.closeSession(user_path_session.session_id).ok());
      assert(pathExists(user_raw));
      assert(pathExists(user_decoded));
      std::remove(user_raw.c_str());
      std::remove(user_decoded.c_str());
    }

    rpc::JsonValue create_params = rpc::JsonValue::object();
    create_params["session_id"] = "log-rpc-2";
    create_params["driver_factory"] = "linux-host";
    create_params["source"] = "firmware";
    create_params["min_level"] = "info";
    auto created = audio_client.call("log.createSession", create_params);
    assert(created.at("session").at("session_id").asString() == "log-rpc-2");
    rpc::JsonValue session_params = rpc::JsonValue::object();
    session_params["session_id"] = "log-rpc-2";
    assert(audio_client.call("log.start", session_params).at("session").at("running").asBool());
    auto read_entries = audio_client.call("log.readEntries", session_params);
    assert(read_entries.at("entries").asArray().size() >= 1);
    assert(read_entries.at("entries").asArray()[0].at("text").asString().find("audio controller") != std::string::npos);
    assert(audio_client.call("log.stop", session_params).at("session").at("running").asBool() == false);
    assert(audio_client.call("log.closeSession", session_params).at("closed").asBool());

    {
      const std::string binary_log = tempPath("binary.raw");
      {
        std::ofstream out(binary_log, std::ios::binary | std::ios::trunc);
        const char bytes[] = {0x00, 0x01, 0x02, static_cast<char>(0xff)};
        out.write(bytes, sizeof(bytes));
      }
      rpc::JsonValue raw_create = rpc::JsonValue::object();
      raw_create["session_id"] = "log-raw-binary";
      raw_create["driver_factory"] = "linux-host";
      raw_create["source"] = binary_log;
      assert(audio_client.call("log.createSession", raw_create).at("session").at("session_id").asString() == "log-raw-binary");
      rpc::JsonValue raw_session = rpc::JsonValue::object();
      raw_session["session_id"] = "log-raw-binary";
      assert(audio_client.call("log.start", raw_session).at("session").at("running").asBool());
      auto raw_result = audio_client.call("log.readRaw", raw_session);
      const auto& raw_item = raw_result.at("chunks").asArray().front();
      assert(raw_item.at("encoding").asString() == "base64");
      assert(raw_item.at("bytes_base64").asString() == "AAEC/w==");
      assert(audio_client.call("log.closeSession", raw_session).at("closed").asBool());
      std::remove(binary_log.c_str());
    }
  }

#if defined(CONFIG_FRAMEWORK_CONFIG)
  {
    assertInlineEndpointSchema(std::string(AUDIO_STUDIO_TEST_ROOT) + "/configs/platform/a2/A2.json");
    assertInlineEndpointSchema(std::string(AUDIO_STUDIO_TEST_ROOT) + "/configs/platform/simulator/simulator.json");
    const auto builtin = audio_studio::rpc::parseJson(
        readFileText(std::string(AUDIO_STUDIO_TEST_ROOT) + "/configs/built-in-algorithm.json"));
    assert(hasModuleType(builtin, "builtin.host"));
    assert(hasModuleType(builtin, "builtin.dai"));
    assert(hasModuleType(builtin, "builtin.file_input"));
    assert(hasModuleType(builtin, "builtin.file_output"));
    assert(!jsonContainsStringValue(builtin, "RUNTIME"));

    audio_studio::framework::config::ConfigService config_service(&drivers.filesystem(), &drivers.os(), &drivers.dynlib());
    audio_studio::framework::config::ConfigCompileRequest request;
    request.input_path = std::string(AUDIO_STUDIO_TEST_ROOT) + "/configs/platform/a2/A2.json";
    request.output_dir = drivers.filesystem().joinPath({
      drivers.os().system().temporaryDirectory(),
      "audio-studio-config-test-" + std::to_string(static_cast<long long>(getpid())),
    });
    (void)drivers.filesystem().remove(request.output_dir);
    request.project_name = "a2_test";
    request.build_tplg = audio_studio::framework::config::kHostSupportsAlsaTplg;
    request.plugin_paths.push_back(AUDIO_STUDIO_CONFIG_TEST_PLUGIN_PATH);

    audio_studio::framework::config::ConfigCompileOutput output;
    assert(config_service.compile(request, output).ok());
    assert(output.ok);
    assert(output.module_type_count == 23);
    assert(output.module_instance_count == 0);
    assert(output.pipeline_count == 3);
    assert(output.runtime_control_count == 8);
    assert(output.install_param_count > 15);
    assert(output.preset_count == 3);
    assert(output.plugin_count == 1);

    audio_studio::drivers::filesystem::FileInfo info;
    assert(drivers.filesystem().stat(output.conf_path, info).ok() && info.size > 0);
    const auto a2_conf = readFileText(output.conf_path);
    assert(a2_conf.find("\"PLAYBACK_MAIN VOLUME Enable\"") != std::string::npos);
    assert(a2_conf.find("\"PLAYBACK_MAIN VOLUME Volume\"") != std::string::npos);
    assert(a2_conf.find("\"PLAYBACK_MAIN VOLUME Mute\"") != std::string::npos);
    assert(a2_conf.find("\"CAPTURE_MAIN VOLUME Enable\"") != std::string::npos);
    assert(a2_conf.find("\"CAPTURE_MAIN VOLUME Volume\"") != std::string::npos);
    assert(a2_conf.find("\"CAPTURE_MAIN VOLUME Mute\"") != std::string::npos);
    assert(a2_conf.find("\"CAPTURE_MAIN SRC Enable\"") != std::string::npos);
    assert(a2_conf.find("\"CAPTURE_MAIN SRC Dither\"") != std::string::npos);
    assert(a2_conf.find("SectionControlBytes.\"PLAYBACK_MAIN CHREMAP Channel Remap\"") != std::string::npos);
    assert(a2_conf.find("SectionControlBytes.\"PLAYBACK_MAIN DELAY Delay Line\"") != std::string::npos);
    assert(a2_conf.find("SectionControlBytes.\"PLAYBACK_MAIN FADER Fader Balance\"") != std::string::npos);
    assert(a2_conf.find("\"PLAYBACK_MAIN CHREMAP Channel Layout\"") == std::string::npos);
    assert(a2_conf.find("\"PLAYBACK_MAIN DELAY Max Delay\"") == std::string::npos);
    assert(a2_conf.find("\"PLAYBACK_MAIN FADER Balance\"") == std::string::npos);
    assert(drivers.filesystem().stat(output.private_bin_path, info).ok() && info.size > 0);
    assert(output.tplg_built == audio_studio::framework::config::kHostSupportsAlsaTplg);
    if (audio_studio::framework::config::kHostSupportsAlsaTplg) {
      assert(drivers.filesystem().stat(output.tplg_path, info).ok() && info.size > 0);
      assert(drivers.filesystem().stat(output.tplg_decode_log_path, info).ok());
      if (output.tplg_decoded) {
        assert(drivers.filesystem().stat(output.tplg_decode_conf_path, info).ok() && info.size > 0);
        assert(output.warnings.empty());
      } else {
        assert(!output.warnings.empty());
      }
    } else {
      bool path_exists = true;
      assert(drivers.filesystem().exists(output.tplg_path, path_exists).ok() && !path_exists);
      assert(drivers.filesystem().exists(output.tplg_decode_conf_path, path_exists).ok() && !path_exists);
      assert(drivers.filesystem().exists(output.alsatplg_log_path, path_exists).ok() && !path_exists);
      assert(drivers.filesystem().exists(output.tplg_decode_log_path, path_exists).ok() && !path_exists);
    }

    const std::string ids = readFileText(output.ids_header_path);
    assert(ids.find("AS_MODULE_TYPE_RATE_SRC") != std::string::npos);
    assert(ids.find("AS_MODULE_TYPE_SERVICE_ASRC") == std::string::npos);
    assert(ids.find("#define AS_PARAM_GAIN_VOLUME_VOLUME_DB 0x7B6FD765u") != std::string::npos);
    assert(ids.find("AS_CONTROL_PLAYBACK_MAIN_VOLUME_VOLUME_DB") != std::string::npos);
    const std::string presets = readFileText(output.preset_header_path);
    assert(presets.find("AS_PRESET_PLAYBACK_MUSIC") != std::string::npos);
    assert(presets.find("AS_PRESET_INSPECTOR_PRESET") != std::string::npos);
    const std::string conf = readFileText(output.conf_path);
    assert(conf.find("SectionVendorTokens.\"sof_sched_tokens\"") != std::string::npos);
    assert(conf.find("SectionVendorTokens.\"sof_comp_tokens\"") != std::string::npos);
    assert(conf.find("SectionVendorTokens.\"sof_dai_tokens\"") != std::string::npos);
    assert(conf.find("type \"scheduler\"") != std::string::npos);
    assert(conf.find("SectionDAI.") != std::string::npos);
    assert(conf.find("SectionBE.") != std::string::npos);
    assert(conf.find("SOF_TKN_DAI_TYPE \"FILE_IO\"") != std::string::npos);
    assert(conf.find("SOF_TKN_DAI_INDEX \"0\"") != std::string::npos);
    assert(conf.find("SOF_TKN_DAI_INDEX \"1\"") != std::string::npos);
    assert(conf.find("stream_name \"FILE_IO_PLAYBACK_DAI0\"") != std::string::npos);
    assert(conf.find("stream_name \"FILE_IO_DSP_FILTER_DAI1\"") != std::string::npos);
    assert(conf.find("SOF_TKN_DAI_TYPE \"VSI_TDM\"") == std::string::npos);
    assert(conf.find("SectionControlMixer.") != std::string::npos);
    assert(conf.find("SectionTLV.") != std::string::npos);
    assert(conf.find("SOF_TKN_PROCESS_TYPE \"CHAN_REMAP\"") != std::string::npos);
    assert(conf.find("SOF_TKN_PROCESS_TYPE \"DELAY_LINE\"") != std::string::npos);
    assert(conf.find("SOF_TKN_PROCESS_TYPE \"FADER_BALANCE\"") != std::string::npos);
    assert(conf.find("SOF_TKN_PROCESS_TYPE \"DSP_FILTER\"") != std::string::npos);
    assert(conf.find("\"PLAYBACK_MAIN DELAY Delay Line\"") != std::string::npos);
    assert(conf.find("\"PLAYBACK_MAIN FADER Fader Balance\"") != std::string::npos);
    assert(conf.find("data [") != std::string::npos);
    const std::string private_payload = readFileText(output.private_bin_path);
    assert(private_payload.find("as-builtin-gain-volume-runtime-json-v1") != std::string::npos);
    assert(private_payload.find("as-builtin-gain-volume-preset-json-v1") != std::string::npos);
    assert(private_payload.find("\"pipelines\"") != std::string::npos);
    assert(private_payload.find("\"dai_id\":\"FILE_IO_PLAYBACK_DAI0\"") != std::string::npos);
    assert(private_payload.find("\"hw_id\":\"FILEIO1\"") != std::string::npos);
    assert(private_payload.find("\"dai_index\":1") != std::string::npos);
    assert(private_payload.find("\"tdm_slots\":2") != std::string::npos);
    assert(private_payload.find("\"config_format\":\"sof-ipc3-bytes-v1\"") != std::string::npos);
    assert(private_payload.find("\"codec_format\"") == std::string::npos);
    assert(private_payload.find("\"config_format\"") != std::string::npos);
    if (audio_studio::framework::config::kHostSupportsAlsaTplg) {
      const std::string alsatplg_log = readFileText(output.alsatplg_log_path);
      assert(alsatplg_log.find("ALSA lib") == std::string::npos);
      if (output.tplg_decoded) {
        const std::string decoded_conf = readFileText(output.tplg_decode_conf_path);
        assert(decoded_conf.find("'FILE_IO_DSP_FILTER_DAI1' {\n\t\ttoken154 154\n\t\ttoken155 155") != std::string::npos);
      }
    }
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
