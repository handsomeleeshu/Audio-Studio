#pragma once

#include <map>
#include <string>
#include <vector>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::framework::audio {

enum class StreamDirection {
  kPlayback,
  kCapture,
};

struct AudioStream {
  std::string id;
  StreamDirection direction = StreamDirection::kPlayback;
  int sample_rate = 48000;
  int channels = 2;
  bool running = false;
};

class AudioService {
public:
  framework::Status create(AudioStream stream);
  framework::Status start(const std::string& id);
  framework::Status stop(const std::string& id);
  framework::Status get(const std::string& id, AudioStream& out) const;
  std::vector<AudioStream> list() const;

private:
  std::map<std::string, AudioStream> streams_;
};

} // namespace audio_studio::framework::audio
