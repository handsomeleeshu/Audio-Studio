#pragma once

#include <string>
#include <vector>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::drivers::transport {

struct TransportCaps {
  size_t max_payload_size = 4096;
  bool reliable = true;
  bool ordered = true;
};

class TransportDriver {
public:
  framework::Status open(std::string name);
  framework::Status write(const std::vector<uint8_t>& data);
  framework::Status read(size_t capacity, std::vector<uint8_t>& out);
  framework::Status flush();
  void close();
  bool isConnected() const;
  const std::string& name() const;
  TransportCaps caps() const;
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
