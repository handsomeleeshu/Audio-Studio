#pragma once

#include <map>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "datalink_manager.hpp"
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
  using AsyncCallback = std::function<void(const framework::Status&, const TransportFrame&)>;

  TransportManager();
  explicit TransportManager(drivers::transport::IDataLinkDevice& device);
  ~TransportManager();

  framework::Status bindDataLinkDevice(drivers::transport::IDataLinkDevice& device,
                                       DataLinkManagerConfig config = {});
  framework::Status openChannel(uint16_t id, std::string service);
  framework::Status closeChannel(uint16_t id);
  void shutdown();
  framework::Status recordTx(const TransportFrame& frame);
  framework::Status recordRx(const TransportFrame& frame);
  framework::Status getChannel(uint16_t id, LogicalChannel& out) const;
  std::vector<LogicalChannel> listChannels() const;
  uint32_t nextSequence();
  framework::Status sendSync(uint16_t channel_id,
                             uint16_t command_id,
                             const std::vector<uint8_t>& payload,
                             TransportFrame& response,
                             uint32_t timeout_ms,
                             uint32_t session_id = 0);
  framework::Status sendAsync(uint16_t channel_id,
                              uint16_t command_id,
                              std::vector<uint8_t> payload,
                              AsyncCallback callback,
                              uint32_t timeout_ms,
                              uint32_t session_id = 0);
  framework::Status drainAsync(uint16_t channel_id);

private:
  struct ChannelRuntime;

  framework::Status requireChannel(uint16_t id, std::shared_ptr<ChannelRuntime>& out) const;
  void channelWorker(const std::shared_ptr<ChannelRuntime>& runtime);

  mutable std::mutex channels_mutex_;
  mutable std::mutex io_mutex_;
  std::map<uint16_t, std::shared_ptr<ChannelRuntime>> channels_;
  std::unique_ptr<DataLinkManager> datalink_;
  drivers::transport::IDataLinkDevice* device_ = nullptr;
  std::atomic<uint32_t> next_sequence_ {1};
};

} // namespace audio_studio::framework::transport
