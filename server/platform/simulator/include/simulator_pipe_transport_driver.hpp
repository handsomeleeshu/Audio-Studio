#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "transport_driver.hpp"

namespace audio_studio::platform::simulator {

class SimulatorPipeTransportDriver final : public drivers::transport::ITransportDriver {
public:
  drivers::transport::TransportResult open(const drivers::transport::TransportConfig& config) override;
  void close() override;
  drivers::transport::TransportResult write(const uint8_t* data, size_t size, uint32_t timeout_ms) override;
  drivers::transport::TransportResult read(uint8_t* buffer, size_t capacity, size_t& actual_size, uint32_t timeout_ms) override;
  drivers::transport::TransportResult flush() override;
  bool isConnected() const override;
  drivers::transport::TransportCaps caps() const override;
  std::string name() const override;

private:
  static size_t optionSize(const drivers::transport::TransportConfig& config,
                           const std::string& key,
                           size_t fallback);
  static std::string optionString(const drivers::transport::TransportConfig& config,
                                  const std::string& key,
                                  const std::string& fallback);

  drivers::transport::TransportResult ensureFile(const std::string& path) const;

  mutable std::mutex mutex_;
  std::string name_;
  std::string rx_path_;
  std::string tx_path_;
  bool connected_ = false;
  bool loopback_ = false;
  size_t mtu_ = 512;
  size_t read_offset_ = 0;
  std::vector<uint8_t> loopback_rx_;
};

} // namespace audio_studio::platform::simulator
