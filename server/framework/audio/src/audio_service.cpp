#include "audio_studio/framework/audio/audio_service.hpp"

#include <utility>

namespace audio_studio::framework::audio {

framework::Status AudioService::create(AudioStream stream) {
  if (stream.id.empty()) return framework::Status::invalidArgument("audio stream id is empty");
  if (stream.sample_rate <= 0) return framework::Status::invalidArgument("audio sample rate must be positive");
  if (stream.channels <= 0) return framework::Status::invalidArgument("audio channel count must be positive");
  if (streams_.find(stream.id) != streams_.end()) return framework::Status::invalidArgument("audio stream already exists: " + stream.id);
  streams_.emplace(stream.id, std::move(stream));
  return framework::Status::success();
}

framework::Status AudioService::start(const std::string& id) {
  auto it = streams_.find(id);
  if (it == streams_.end()) return framework::Status::unavailable("audio stream not found: " + id);
  it->second.running = true;
  return framework::Status::success();
}

framework::Status AudioService::stop(const std::string& id) {
  auto it = streams_.find(id);
  if (it == streams_.end()) return framework::Status::unavailable("audio stream not found: " + id);
  it->second.running = false;
  return framework::Status::success();
}

framework::Status AudioService::get(const std::string& id, AudioStream& out) const {
  const auto it = streams_.find(id);
  if (it == streams_.end()) return framework::Status::unavailable("audio stream not found: " + id);
  out = it->second;
  return framework::Status::success();
}

std::vector<AudioStream> AudioService::list() const {
  std::vector<AudioStream> out;
  out.reserve(streams_.size());
  for (const auto& item : streams_) out.push_back(item.second);
  return out;
}

} // namespace audio_studio::framework::audio
