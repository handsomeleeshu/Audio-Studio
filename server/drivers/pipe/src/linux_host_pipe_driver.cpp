#include "linux_host_pipe_driver.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace audio_studio::drivers::pipe {

LinuxHostPipeStream::LinuxHostPipeStream(LinuxHostPipeDriver& driver, PipeType type) : driver_(driver), type_(type) {}

DriverResult LinuxHostPipeStream::open(const PipeConfig& config) {
  auto it = driver_.pipes_.find(config.endpoint.path);
  if (it == driver_.pipes_.end()) return DriverResult::unavailable("pipe not found: " + config.endpoint.path);
  if (it->second.type != config.type) return DriverResult::invalidArgument("pipe type mismatch");
  type_ = config.type;
  open_path_ = config.endpoint.path;
  return DriverResult::success();
}

DriverResult LinuxHostPipeStream::read(void* buffer, size_t capacity, size_t& read_bytes, uint32_t /*timeout_ms*/) {
  if (open_path_.empty()) return DriverResult::unavailable("pipe is not open");
  if (buffer == nullptr && capacity > 0) return DriverResult::invalidArgument("pipe read buffer is null");
  auto& pipe = driver_.pipes_[open_path_].buffer;
  read_bytes = std::min(capacity, pipe.size());
  if (read_bytes > 0) std::memcpy(buffer, pipe.data(), read_bytes);
  pipe.erase(pipe.begin(), pipe.begin() + read_bytes);
  return DriverResult::success();
}

DriverResult LinuxHostPipeStream::write(const void* data, size_t size, size_t& written_bytes, uint32_t /*timeout_ms*/) {
  if (open_path_.empty()) return DriverResult::unavailable("pipe is not open");
  if (data == nullptr && size > 0) return DriverResult::invalidArgument("pipe write buffer is null");
  auto& pipe = driver_.pipes_[open_path_].buffer;
  const auto* bytes = static_cast<const uint8_t*>(data);
  pipe.insert(pipe.end(), bytes, bytes + size);
  written_bytes = size;
  return DriverResult::success();
}

DriverResult LinuxHostPipeStream::flush() {
  if (open_path_.empty()) return DriverResult::unavailable("pipe is not open");
  return DriverResult::success();
}

void LinuxHostPipeStream::close() {
  open_path_.clear();
}

bool LinuxHostPipeStream::isOpen() const {
  return !open_path_.empty();
}

std::unique_ptr<IPipeStream> LinuxHostPipeDriver::createPipeStream(PipeType type) {
  return std::make_unique<LinuxHostPipeStream>(*this, type);
}

DriverResult LinuxHostPipeDriver::createPipe(const PipeEndpoint& endpoint, PipeType type) {
  if (endpoint.path.empty()) return framework::Status::invalidArgument("pipe path is empty");
  if (pipes_.find(endpoint.path) != pipes_.end()) return framework::Status::invalidArgument("pipe already exists: " + endpoint.path);
  pipes_.emplace(endpoint.path, PipeState{type, {}});
  return framework::Status::success();
}

DriverResult LinuxHostPipeDriver::removePipe(const PipeEndpoint& endpoint, PipeType /*type*/) {
  const auto erased = pipes_.erase(endpoint.path);
  if (erased == 0) return framework::Status::unavailable("pipe not found: " + endpoint.path);
  return framework::Status::success();
}

DriverResult LinuxHostPipeDriver::exists(const PipeEndpoint& endpoint, bool& result) {
  result = pipes_.find(endpoint.path) != pipes_.end();
  return framework::Status::success();
}

} // namespace audio_studio::drivers::pipe
