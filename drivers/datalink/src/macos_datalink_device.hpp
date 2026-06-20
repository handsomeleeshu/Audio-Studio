#pragma once

#include "datalink_device.hpp"

namespace audio_studio::drivers::datalink {

class MacOsDataLinkDevice final : public IDataLinkDevice {
public:
  DataLinkResult open(const DataLinkDeviceConfig& config) override;
  void close() override;
  DataLinkResult writeBlock(const uint8_t* data, size_t size, uint32_t timeout_ms) override;
  DataLinkResult readBlock(uint8_t* buffer, size_t capacity, size_t& actual_size, uint32_t timeout_ms) override;
  DataLinkResult flush() override;
  bool isConnected() const override;
  DataLinkDeviceCaps caps() const override;
  std::string name() const override;

  size_t bytesWritten() const;
  size_t bytesRead() const;

private:
  std::string name_;
  bool connected_ = false;
  size_t bytes_written_ = 0;
  size_t bytes_read_ = 0;
  std::vector<uint8_t> rx_buffer_;
};

} // namespace audio_studio::drivers::datalink
