#include "audio_studio/framework/transport/transport_manager.hpp"

#include <utility>

namespace audio_studio::framework::transport {

framework::Status TransportManager::openChannel(uint16_t id, std::string service) {
  if (id == 0) return framework::Status::invalidArgument("transport channel id is zero");
  if (service.empty()) return framework::Status::invalidArgument("transport service is empty");
  if (channels_.find(id) != channels_.end()) return framework::Status::invalidArgument("transport channel already exists");
  channels_.emplace(id, LogicalChannel{id, std::move(service), true, 0, 0});
  return framework::Status::success();
}

framework::Status TransportManager::closeChannel(uint16_t id) {
  auto it = channels_.find(id);
  if (it == channels_.end()) return framework::Status::unavailable("transport channel not found");
  it->second.open = false;
  return framework::Status::success();
}

framework::Status TransportManager::recordTx(const TransportFrame& frame) {
  auto it = channels_.find(frame.channel_id);
  if (it == channels_.end()) return framework::Status::unavailable("transport channel not found");
  if (!it->second.open) return framework::Status::unavailable("transport channel is closed");
  ++it->second.frames_sent;
  return framework::Status::success();
}

framework::Status TransportManager::recordRx(const TransportFrame& frame) {
  auto it = channels_.find(frame.channel_id);
  if (it == channels_.end()) return framework::Status::unavailable("transport channel not found");
  if (!it->second.open) return framework::Status::unavailable("transport channel is closed");
  ++it->second.frames_received;
  return framework::Status::success();
}

framework::Status TransportManager::getChannel(uint16_t id, LogicalChannel& out) const {
  const auto it = channels_.find(id);
  if (it == channels_.end()) return framework::Status::unavailable("transport channel not found");
  out = it->second;
  return framework::Status::success();
}

std::vector<LogicalChannel> TransportManager::listChannels() const {
  std::vector<LogicalChannel> out;
  out.reserve(channels_.size());
  for (const auto& item : channels_) out.push_back(item.second);
  return out;
}

uint32_t TransportManager::nextSequence() {
  return next_sequence_++;
}

} // namespace audio_studio::framework::transport
