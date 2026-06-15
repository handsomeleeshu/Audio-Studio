#include "audio_studio/drivers/log/log_device.hpp"

#include <utility>

namespace audio_studio::drivers::log {

framework::Status LogDevice::open(std::string source) {
  if (source.empty()) return framework::Status::invalidArgument("log source is empty");
  source_ = std::move(source);
  open_ = true;
  return framework::Status::success();
}

framework::Status LogDevice::start() {
  if (!open_) return framework::Status::unavailable("log device is not open");
  running_ = true;
  return framework::Status::success();
}

framework::Status LogDevice::appendChunk(LogChunk chunk) {
  if (!open_) return framework::Status::unavailable("log device is not open");
  if (chunk.bytes.empty()) return framework::Status::invalidArgument("log chunk is empty");
  chunks_.push_back(std::move(chunk));
  ++chunks_written_;
  return framework::Status::success();
}

framework::Status LogDevice::readChunk(LogChunk& out) {
  if (!running_) return framework::Status::unavailable("log device is not running");
  if (chunks_.empty()) return framework::Status::unavailable("no log chunk available");
  out = std::move(chunks_.front());
  chunks_.erase(chunks_.begin());
  ++chunks_read_;
  return framework::Status::success();
}

framework::Status LogDevice::stop() {
  if (!open_) return framework::Status::unavailable("log device is not open");
  running_ = false;
  return framework::Status::success();
}

void LogDevice::close() {
  open_ = false;
  running_ = false;
  chunks_.clear();
}

LogStats LogDevice::stats() const {
  return {chunks_written_, chunks_read_, running_};
}

} // namespace audio_studio::drivers::log
