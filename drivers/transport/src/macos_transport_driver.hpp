#pragma once

#include "transport_driver.hpp"

namespace audio_studio::drivers::transport {

class MacOsTransportDriver final : public ITransportDriver {
public:
  TransportResult open(const TransportConfig& config) override;
  void close() override;
  TransportResult write(const uint8_t* data, size_t size, uint32_t timeout_ms) override;
  TransportResult read(uint8_t* buffer, size_t capacity, size_t& actual_size, uint32_t timeout_ms) override;
  TransportResult flush() override;
  bool isConnected() const override;
  TransportCaps caps() const override;
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

} // namespace audio_studio::drivers::transport
