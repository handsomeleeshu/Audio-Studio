#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "datalink_frame.hpp"
#include "transport_driver.hpp"

namespace audio_studio::framework::transport {

struct DataLinkManagerConfig {
  uint32_t ack_timeout_ms = 100;
  uint32_t max_retries = 3;
};

struct DataLinkManagerStats {
  size_t packets_sent = 0;
  size_t packets_received = 0;
  size_t fragments_sent = 0;
  size_t fragments_received = 0;
  size_t retries = 0;
};

class DataLinkManager {
public:
  DataLinkManager(drivers::transport::IDataLinkDevice& device, DataLinkManagerConfig config = {});

  framework::Status sendPacket(const std::vector<uint8_t>& payload, uint32_t timeout_ms);
  framework::Status receivePacket(std::vector<uint8_t>& payload, uint32_t timeout_ms);
  const DataLinkManagerStats& stats() const;

private:
  framework::Status readFrame(DataLinkFrame& frame, uint32_t timeout_ms);
  framework::Status sendAck(const DataLinkFrame& frame, bool ok, uint32_t timeout_ms);

  drivers::transport::IDataLinkDevice& device_;
  DataLinkManagerConfig config_;
  DataLinkManagerStats stats_;
  uint32_t next_link_sequence_ = 1;
  std::vector<uint8_t> rx_buffer_;
};

} // namespace audio_studio::framework::transport
