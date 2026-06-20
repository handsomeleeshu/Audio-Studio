#include "transport_manager.hpp"

#include <condition_variable>
#include <deque>
#include <thread>
#include <utility>

#include "frame_codec.hpp"

namespace audio_studio::framework::transport {

struct TransportManager::ChannelRuntime {
  struct AsyncRequest {
    uint16_t command_id = 0;
    std::vector<uint8_t> payload;
    AsyncCallback callback;
    uint32_t timeout_ms = 0;
    uint32_t session_id = 0;
  };

  explicit ChannelRuntime(LogicalChannel channel_value) : channel(std::move(channel_value)) {}

  LogicalChannel channel;
  mutable std::mutex mutex;
  std::condition_variable cv;
  std::deque<AsyncRequest> queue;
  bool active = false;
  bool stopping = false;
  std::thread worker;
};

TransportManager::TransportManager() = default;

TransportManager::TransportManager(drivers::transport::IDataLinkDevice& device) {
  (void)bindDataLinkDevice(device);
}

TransportManager::~TransportManager() {
  shutdown();
}

framework::Status TransportManager::bindDataLinkDevice(drivers::transport::IDataLinkDevice& device,
                                                       DataLinkManagerConfig config) {
  if (!device.isConnected()) return framework::Status::unavailable("data-link device is not connected");
  std::lock_guard<std::mutex> lock(io_mutex_);
  device_ = &device;
  datalink_ = std::make_unique<DataLinkManager>(device, config);
  return framework::Status::success();
}

framework::Status TransportManager::openChannel(uint16_t id, std::string service) {
  if (id == 0) return framework::Status::invalidArgument("transport channel id is zero");
  if (service.empty()) return framework::Status::invalidArgument("transport service is empty");
  std::lock_guard<std::mutex> lock(channels_mutex_);
  if (channels_.find(id) != channels_.end()) return framework::Status::invalidArgument("transport channel already exists");
  auto runtime = std::make_shared<ChannelRuntime>(LogicalChannel{id, std::move(service), true, 0, 0});
  runtime->worker = std::thread([this, runtime] { channelWorker(runtime); });
  channels_.emplace(id, std::move(runtime));
  return framework::Status::success();
}

framework::Status TransportManager::closeChannel(uint16_t id) {
  std::shared_ptr<ChannelRuntime> runtime;
  auto status = requireChannel(id, runtime);
  if (!status.ok()) return status;
  (void)drainAsync(id);
  {
    std::lock_guard<std::mutex> lock(runtime->mutex);
    runtime->channel.open = false;
    runtime->stopping = true;
  }
  runtime->cv.notify_all();
  if (runtime->worker.joinable()) runtime->worker.join();
  std::lock_guard<std::mutex> lock(channels_mutex_);
  channels_.erase(id);
  return framework::Status::success();
}

void TransportManager::shutdown() {
  std::vector<uint16_t> ids;
  {
    std::lock_guard<std::mutex> lock(channels_mutex_);
    for (const auto& item : channels_) ids.push_back(item.first);
  }
  for (const auto id : ids) (void)closeChannel(id);
}

framework::Status TransportManager::recordTx(const TransportFrame& frame) {
  std::shared_ptr<ChannelRuntime> runtime;
  auto status = requireChannel(frame.channel_id, runtime);
  if (!status.ok()) return status;
  std::lock_guard<std::mutex> lock(runtime->mutex);
  if (!runtime->channel.open) return framework::Status::unavailable("transport channel is closed");
  ++runtime->channel.frames_sent;
  return framework::Status::success();
}

framework::Status TransportManager::recordRx(const TransportFrame& frame) {
  std::shared_ptr<ChannelRuntime> runtime;
  auto status = requireChannel(frame.channel_id, runtime);
  if (!status.ok()) return status;
  std::lock_guard<std::mutex> lock(runtime->mutex);
  if (!runtime->channel.open) return framework::Status::unavailable("transport channel is closed");
  ++runtime->channel.frames_received;
  return framework::Status::success();
}

framework::Status TransportManager::getChannel(uint16_t id, LogicalChannel& out) const {
  std::shared_ptr<ChannelRuntime> runtime;
  auto status = requireChannel(id, runtime);
  if (!status.ok()) return status;
  std::lock_guard<std::mutex> lock(runtime->mutex);
  out = runtime->channel;
  return framework::Status::success();
}

std::vector<LogicalChannel> TransportManager::listChannels() const {
  std::vector<LogicalChannel> out;
  std::lock_guard<std::mutex> lock(channels_mutex_);
  out.reserve(channels_.size());
  for (const auto& item : channels_) {
    std::lock_guard<std::mutex> channel_lock(item.second->mutex);
    out.push_back(item.second->channel);
  }
  return out;
}

uint32_t TransportManager::nextSequence() {
  return next_sequence_.fetch_add(1);
}

framework::Status TransportManager::sendSync(uint16_t channel_id,
                                             uint16_t command_id,
                                             const std::vector<uint8_t>& payload,
                                             TransportFrame& response,
                                             uint32_t timeout_ms,
                                             uint32_t session_id) {
  response = {};
  std::shared_ptr<ChannelRuntime> runtime;
  auto status = requireChannel(channel_id, runtime);
  if (!status.ok()) return status;
  {
    std::lock_guard<std::mutex> lock(runtime->mutex);
    if (!runtime->channel.open) return framework::Status::unavailable("transport channel is closed");
  }

  TransportFrame request;
  request.channel_id = channel_id;
  request.service_id = channel_id;
  request.command_id = command_id;
  request.flags = kTransportFrameRequest | kTransportFrameAckRequired;
  request.sequence_id = nextSequence();
  request.session_id = session_id;
  request.payload = payload;

  const auto encoded = FrameCodec::encode(request);
  std::lock_guard<std::mutex> io_lock(io_mutex_);
  if (!datalink_) return framework::Status::unavailable("transport data-link is not bound");
  status = datalink_->sendPacket(encoded, timeout_ms);
  if (!status.ok()) return status;
  status = recordTx(request);
  if (!status.ok()) return status;

  std::vector<uint8_t> response_payload;
  status = datalink_->receivePacket(response_payload, timeout_ms);
  if (!status.ok()) return status;
  status = FrameCodec::decode(response_payload.data(), response_payload.size(), response);
  if (!status.ok()) return status;
  if (response.channel_id != request.channel_id || response.sequence_id != request.sequence_id) {
    return framework::Status::unavailable("transport ACK does not match request");
  }
  if ((response.flags & (kTransportFrameAck | kTransportFrameResponse)) == 0) {
    return framework::Status::unavailable("transport response is not an ACK");
  }
  return recordRx(response);
}

framework::Status TransportManager::sendAsync(uint16_t channel_id,
                                              uint16_t command_id,
                                              std::vector<uint8_t> payload,
                                              AsyncCallback callback,
                                              uint32_t timeout_ms,
                                              uint32_t session_id) {
  std::shared_ptr<ChannelRuntime> runtime;
  auto status = requireChannel(channel_id, runtime);
  if (!status.ok()) return status;
  {
    std::lock_guard<std::mutex> lock(runtime->mutex);
    if (!runtime->channel.open) return framework::Status::unavailable("transport channel is closed");
    runtime->queue.push_back({command_id, std::move(payload), std::move(callback), timeout_ms, session_id});
  }
  runtime->cv.notify_all();
  return framework::Status::success();
}

framework::Status TransportManager::drainAsync(uint16_t channel_id) {
  std::shared_ptr<ChannelRuntime> runtime;
  auto status = requireChannel(channel_id, runtime);
  if (!status.ok()) return status;
  std::unique_lock<std::mutex> lock(runtime->mutex);
  runtime->cv.wait(lock, [&] { return runtime->queue.empty() && !runtime->active; });
  return framework::Status::success();
}

framework::Status TransportManager::requireChannel(uint16_t id, std::shared_ptr<ChannelRuntime>& out) const {
  std::lock_guard<std::mutex> lock(channels_mutex_);
  const auto it = channels_.find(id);
  if (it == channels_.end()) return framework::Status::unavailable("transport channel not found");
  out = it->second;
  return framework::Status::success();
}

void TransportManager::channelWorker(const std::shared_ptr<ChannelRuntime>& runtime) {
  while (true) {
    ChannelRuntime::AsyncRequest request;
    {
      std::unique_lock<std::mutex> lock(runtime->mutex);
      runtime->cv.wait(lock, [&] { return runtime->stopping || !runtime->queue.empty(); });
      if (runtime->stopping && runtime->queue.empty()) break;
      request = std::move(runtime->queue.front());
      runtime->queue.pop_front();
      runtime->active = true;
    }

    TransportFrame response;
    auto status = sendSync(runtime->channel.id, request.command_id, request.payload, response,
                           request.timeout_ms, request.session_id);
    if (request.callback) request.callback(status, response);

    {
      std::lock_guard<std::mutex> lock(runtime->mutex);
      runtime->active = false;
    }
    runtime->cv.notify_all();
  }
}

} // namespace audio_studio::framework::transport
