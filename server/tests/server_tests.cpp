#include <cassert>
#include <chrono>
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

bool hasModuleInstance(const audio_studio::rpc::JsonValue& root,
                       const std::string& inst_id,
                       const std::string& module_type) {
  for (const auto& item : root.at("module_instances").asArray()) {
    if (item.at("inst_id").asString() == inst_id && item.at("module_type").asString() == module_type) {
      return true;
    }
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

void assertModuleizedEndpointSchema(const std::string& path) {
  const std::string text = readFileText(path);
  assert(text.find("inspecrot") == std::string::npos);
  assert(text.find("inspetpr") == std::string::npos);
  assert(text.find("\"RUNTIME\"") == std::string::npos);

  const auto root = audio_studio::rpc::parseJson(text);
  assert(!jsonContainsStringValue(root, "RUNTIME"));
  assert(root.has("module_instances"));
  assert(root.has("pipelines"));
  assert(hasPreset(root, "inspector_preset", "inspector"));
  assert(hasModuleInstance(root, "PLAY_HOST", "builtin.host"));
  assert(hasModuleInstance(root, "PLAY_FILEIO_DAI", "builtin.dai"));
  assert(hasModuleInstance(root, "CAP_HOST", "builtin.host"));
  assert(hasModuleInstance(root, "CAP_FILEIO_DAI", "builtin.dai"));

  if (root.has("resource_catalog")) {
    assert(!root.at("resource_catalog").has("audio_endpoints"));
  }

  for (const auto& pipeline : root.at("pipelines").asArray()) {
    assert(!pipeline.has("ports"));
    for (const auto& node : pipeline.at("nodes").asArray()) {
      assert(node.at("kind").asString() == "module");
      assert(node.has("inst_ref"));
    }
  }
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
  }

#if defined(CONFIG_FRAMEWORK_CONFIG)
  {
    assertModuleizedEndpointSchema(std::string(AUDIO_STUDIO_TEST_ROOT) + "/configs/platform/a2/A2.json");
    assertModuleizedEndpointSchema(std::string(AUDIO_STUDIO_TEST_ROOT) + "/configs/platform/simulator/simulator.json");
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
    assert(output.module_instance_count == 14);
    assert(output.pipeline_count == 3);
    assert(output.runtime_control_count == 19);
    assert(output.install_param_count == 15);
    assert(output.preset_count == 3);
    assert(output.plugin_count == 1);

    audio_studio::drivers::filesystem::FileInfo info;
    assert(drivers.filesystem().stat(output.conf_path, info).ok() && info.size > 0);
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
    assert(conf.find("SectionControlBytes.") != std::string::npos);
    assert(conf.find("SOF_TKN_PROCESS_TYPE \"CHAN_REMAP\"") != std::string::npos);
    assert(conf.find("SOF_TKN_PROCESS_TYPE \"DELAY_LINE\"") != std::string::npos);
    assert(conf.find("SOF_TKN_PROCESS_TYPE \"FADER_BALANCE\"") != std::string::npos);
    assert(conf.find("SOF_TKN_PROCESS_TYPE \"DSP_FILTER\"") != std::string::npos);
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
