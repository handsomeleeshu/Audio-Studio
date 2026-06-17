#pragma once

#include <map>
#include <string>
#include <vector>

#include "status.hpp"
#include "transport_frame.hpp"

namespace audio_studio::framework::transport {

struct LogicalChannel {
  uint16_t id = 0;
  std::string service;
  bool open = false;
  size_t frames_sent = 0;
  size_t frames_received = 0;
};

class TransportManager {
public:
  framework::Status openChannel(uint16_t id, std::string service);
  framework::Status closeChannel(uint16_t id);
  framework::Status recordTx(const TransportFrame& frame);
  framework::Status recordRx(const TransportFrame& frame);
  framework::Status getChannel(uint16_t id, LogicalChannel& out) const;
  std::vector<LogicalChannel> listChannels() const;
  uint32_t nextSequence();

private:
  std::map<uint16_t, LogicalChannel> channels_;
  uint32_t next_sequence_ = 1;
};

} // namespace audio_studio::framework::transport
