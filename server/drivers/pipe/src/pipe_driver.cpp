#include "audio_studio/drivers/pipe/pipe_driver.hpp"

#include <algorithm>
#include <utility>

namespace audio_studio::drivers::pipe {

framework::Status PipeDriver::createPipe(PipeEndpoint endpoint, PipeType type) {
  if (endpoint.path.empty()) return framework::Status::invalidArgument("pipe path is empty");
  if (pipes_.find(endpoint.path) != pipes_.end()) return framework::Status::invalidArgument("pipe already exists: " + endpoint.path);
  pipes_.emplace(std::move(endpoint.path), PipeState{type, {}});
  return framework::Status::success();
}

framework::Status PipeDriver::removePipe(const PipeEndpoint& endpoint) {
  const auto erased = pipes_.erase(endpoint.path);
  if (erased == 0) return framework::Status::unavailable("pipe not found: " + endpoint.path);
  if (open_path_ == endpoint.path) open_path_.clear();
  return framework::Status::success();
}

framework::Status PipeDriver::exists(const PipeEndpoint& endpoint, bool& out) const {
  out = pipes_.find(endpoint.path) != pipes_.end();
  return framework::Status::success();
}

framework::Status PipeDriver::open(const PipeEndpoint& endpoint) {
  if (pipes_.find(endpoint.path) == pipes_.end()) return framework::Status::unavailable("pipe not found: " + endpoint.path);
  open_path_ = endpoint.path;
  return framework::Status::success();
}

framework::Status PipeDriver::write(const std::vector<uint8_t>& data) {
  if (open_path_.empty()) return framework::Status::unavailable("pipe is not open");
  auto& buffer = pipes_[open_path_].buffer;
  buffer.insert(buffer.end(), data.begin(), data.end());
  return framework::Status::success();
}

framework::Status PipeDriver::read(size_t capacity, std::vector<uint8_t>& out) {
  if (open_path_.empty()) return framework::Status::unavailable("pipe is not open");
  auto& buffer = pipes_[open_path_].buffer;
  const auto count = std::min(capacity, buffer.size());
  out.assign(buffer.begin(), buffer.begin() + count);
  buffer.erase(buffer.begin(), buffer.begin() + count);
  return framework::Status::success();
}

void PipeDriver::close() {
  open_path_.clear();
}

bool PipeDriver::isOpen() const {
  return !open_path_.empty();
}

} // namespace audio_studio::drivers::pipe
