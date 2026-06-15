#pragma once

#include <map>
#include <string>
#include <vector>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::drivers::pipe {

enum class PipeType {
  kFifo,
  kNamedPipe,
};

struct PipeEndpoint {
  std::string path;
};

class PipeDriver {
public:
  framework::Status createPipe(PipeEndpoint endpoint, PipeType type);
  framework::Status removePipe(const PipeEndpoint& endpoint);
  framework::Status exists(const PipeEndpoint& endpoint, bool& out) const;
  framework::Status open(const PipeEndpoint& endpoint);
  framework::Status write(const std::vector<uint8_t>& data);
  framework::Status read(size_t capacity, std::vector<uint8_t>& out);
  void close();
  bool isOpen() const;

private:
  struct PipeState {
    PipeType type = PipeType::kFifo;
    std::vector<uint8_t> buffer;
  };

  std::map<std::string, PipeState> pipes_;
  std::string open_path_;
};

} // namespace audio_studio::drivers::pipe
