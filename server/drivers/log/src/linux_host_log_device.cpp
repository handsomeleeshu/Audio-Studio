#include "linux_host_log_device.hpp"

#include <utility>

namespace audio_studio::drivers::log {

namespace {

class LinuxHostLogDeviceFactory final : public ILogDeviceFactory {
public:
  std::string name() const override { return "linux-host"; }
  std::unique_ptr<ILogDevice> create(const LogDeviceConfig& config) const override {
    auto device = std::make_unique<LinuxHostLogDevice>();
    if (!device->open(config).ok()) return nullptr;
    return device;
  }
};

const bool kLinuxHostLogDeviceRegistered = [] {
  auto status = LogDeviceRegistry::instance().registerFactory(std::make_unique<LinuxHostLogDeviceFactory>());
  (void)status;
  return true;
}();

} // namespace

LogResult LinuxHostLogDevice::open(const LogDeviceConfig& config) {
  if (config.source.empty()) return LogResult::invalidArgument("log source is empty");
  config_ = config;
  open_ = true;
  return LogResult::success();
}

LogResult LinuxHostLogDevice::configure(const LogDeviceConfig& config) {
  if (!open_) return LogResult::unavailable("log device is not open");
  if (config.source.empty()) return LogResult::invalidArgument("log source is empty");
  config_ = config;
  return LogResult::success();
}

LogResult LinuxHostLogDevice::start() {
  if (!open_) return LogResult::unavailable("log device is not open");
  if (chunks_.empty()) {
    chunks_.push_back({1, {0xaa, 0xbb}});
    ++chunks_written_;
  }
  running_ = true;
  return LogResult::success();
}

LogResult LinuxHostLogDevice::stop() {
  if (!open_) return LogResult::unavailable("log device is not open");
  running_ = false;
  return LogResult::success();
}

LogResult LinuxHostLogDevice::readChunk(LogRawChunk& chunk, uint32_t /*timeout_ms*/) {
  if (!running_) return LogResult::unavailable("log device is not running");
  if (chunks_.empty()) return LogResult::unavailable("no log chunk available");
  chunk = std::move(chunks_.front());
  chunks_.erase(chunks_.begin());
  ++chunks_read_;
  return LogResult::success();
}

LogResult LinuxHostLogDevice::getStats(LogDeviceStats& stats) {
  stats = {chunks_written_, chunks_read_, running_};
  return LogResult::success();
}

void LinuxHostLogDevice::close() {
  open_ = false;
  running_ = false;
  chunks_.clear();
}

LogResult LinuxHostLogDevice::appendChunk(LogRawChunk chunk) {
  if (!open_) return LogResult::unavailable("log device is not open");
  if (chunk.bytes.empty()) return LogResult::invalidArgument("log chunk is empty");
  chunks_.push_back(std::move(chunk));
  ++chunks_written_;
  return LogResult::success();
}

} // namespace audio_studio::drivers::log
