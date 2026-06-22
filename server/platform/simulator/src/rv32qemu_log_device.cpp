#include "log_device.hpp"
#include "transport_manager.hpp"
#include "ac_transport_channel.h"

#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>
#include <utility>

namespace audio_studio::platform::simulator {
namespace {

constexpr uint32_t kLogControllerReadTimeoutMs = 100;

bool isTransportReadTimeout(const framework::Status& status) {
  return status.code() == framework::StatusCode::kUnavailable &&
         status.message().find("timed out") != std::string::npos;
}

uint32_t transportTimeoutForLogRead(uint32_t timeout_ms) {
  return std::max<uint32_t>(timeout_ms + kLogControllerReadTimeoutMs + 250u, 500u);
}

drivers::log::LogResult transportPayloadStatus(const framework::transport::TransportFrame& response) {
  if ((response.flags & framework::transport::kTransportFrameError) == 0) {
    return drivers::log::LogResult::success();
  }
  const std::string message(response.payload.begin(), response.payload.end());
  return drivers::log::LogResult::unavailable(message.empty() ? "transport log command failed" : message);
}

std::vector<uint8_t> sampleLogBytes(const std::string& source) {
  std::string text;
  if (!source.empty() && source != "firmware") {
    std::ifstream input(source, std::ios::binary);
    if (input) {
      std::ostringstream out;
      out << input.rdbuf();
      text = out.str();
    }
  }
  if (text.empty()) {
    text = "info|FW|rv32qemu audio controller log channel open\n"
           "debug|TRP|transport manager log read request\n"
           "warning|DL|rv32qemu simulator pipe waiting for firmware peer\n";
  }
  return {text.begin(), text.end()};
}

class Rv32QemuLogDevice final : public drivers::log::ILogDevice {
public:
  drivers::log::LogResult open(const drivers::log::LogDeviceConfig& config) override {
    if (config.source.empty()) return drivers::log::LogResult::invalidArgument("rv32qemu log source is empty");
    config_ = config;
    open_ = true;
    return drivers::log::LogResult::success();
  }

  drivers::log::LogResult configure(const drivers::log::LogDeviceConfig& config) override {
    if (!open_) return drivers::log::LogResult::unavailable("rv32qemu log device is not open");
    if (config.source.empty()) return drivers::log::LogResult::invalidArgument("rv32qemu log source is empty");
    config_ = config;
    return drivers::log::LogResult::success();
  }

  drivers::log::LogResult start() override {
    if (!open_) return drivers::log::LogResult::unavailable("rv32qemu log device is not open");
    chunks_.clear();
    manager_ = &framework::transport::TransportManager::instance();
    if (!manager_->isDataLinkConfigured()) {
      manager_ = nullptr;
      chunks_.push_back({next_sequence_++, sampleLogBytes(config_.source)});
      ++chunks_written_;
      running_ = true;
      return drivers::log::LogResult::success();
    }

    auto status = manager_->openChannel(AC_TRANSPORT_CHANNEL_LOG, "log");
    if (!status.ok()) {
      manager_ = nullptr;
      return status;
    }

    framework::transport::TransportFrame response;
    const std::vector<uint8_t> source(config_.source.begin(), config_.source.end());
    status = manager_->sendSync(AC_TRANSPORT_CHANNEL_LOG, AC_TRANSPORT_LOG_OPEN, source, response, 1000);
    if (!status.ok()) {
      (void)manager_->closeChannel(AC_TRANSPORT_CHANNEL_LOG);
      manager_ = nullptr;
      return status;
    }
    status = transportPayloadStatus(response);
    if (!status.ok()) {
      (void)manager_->closeChannel(AC_TRANSPORT_CHANNEL_LOG);
      manager_ = nullptr;
      return status;
    }
    if (!response.payload.empty()) {
      chunks_.push_back({next_sequence_++, response.payload});
      ++chunks_written_;
    }
    running_ = true;
    transport_ready_ = true;
    return drivers::log::LogResult::success();
  }

  drivers::log::LogResult stop() override {
    if (!open_) return drivers::log::LogResult::unavailable("rv32qemu log device is not open");
    if (manager_) {
      if (transport_ready_) {
        framework::transport::TransportFrame response;
        (void)manager_->sendSync(AC_TRANSPORT_CHANNEL_LOG, AC_TRANSPORT_LOG_CLOSE, {}, response, 500);
      }
      (void)manager_->closeChannel(AC_TRANSPORT_CHANNEL_LOG);
      manager_ = nullptr;
    }
    transport_ready_ = false;
    running_ = false;
    return drivers::log::LogResult::success();
  }

  drivers::log::LogResult readChunk(drivers::log::LogRawChunk& chunk, uint32_t timeout_ms) override {
    if (!running_) return drivers::log::LogResult::unavailable("rv32qemu log device is not running");
    if (!chunks_.empty()) {
      chunk = std::move(chunks_.front());
      chunks_.erase(chunks_.begin());
      ++chunks_read_;
      return drivers::log::LogResult::success();
    }
    if (!transport_ready_ || !manager_) return drivers::log::LogResult::unavailable("no rv32qemu log chunk available");

    framework::transport::TransportFrame response;
    const auto status = manager_->sendSync(AC_TRANSPORT_CHANNEL_LOG, AC_TRANSPORT_LOG_READ, {}, response,
                                           transportTimeoutForLogRead(timeout_ms));
    if (!status.ok()) {
      if (isTransportReadTimeout(status)) {
        chunk.sequence = next_sequence_;
        chunk.bytes.clear();
        return drivers::log::LogResult::success();
      }
      return status;
    }
    const auto payload_status = transportPayloadStatus(response);
    if (!payload_status.ok()) return payload_status;
    if (response.payload.empty()) {
      chunk.sequence = next_sequence_;
      chunk.bytes.clear();
      return drivers::log::LogResult::success();
    }
    chunk.sequence = next_sequence_++;
    chunk.bytes = std::move(response.payload);
    ++chunks_read_;
    return drivers::log::LogResult::success();
  }

  drivers::log::LogResult getStats(drivers::log::LogDeviceStats& stats) override {
    stats = {chunks_written_, chunks_read_, running_};
    return drivers::log::LogResult::success();
  }

  void close() override {
    if (running_) (void)stop();
    chunks_.clear();
    open_ = false;
  }

private:
  drivers::log::LogDeviceConfig config_;
  bool open_ = false;
  bool running_ = false;
  bool transport_ready_ = false;
  uint32_t next_sequence_ = 1;
  size_t chunks_written_ = 0;
  size_t chunks_read_ = 0;
  std::vector<drivers::log::LogRawChunk> chunks_;
  framework::transport::TransportManager* manager_ = nullptr;
};

class Rv32QemuLogDeviceFactory final : public drivers::log::ILogDeviceFactory {
public:
  explicit Rv32QemuLogDeviceFactory(std::string factory_name) : factory_name_(std::move(factory_name)) {}
  std::string name() const override { return factory_name_; }
  std::unique_ptr<drivers::log::ILogDevice> create(const drivers::log::LogDeviceConfig& config) const override {
    auto device = std::make_unique<Rv32QemuLogDevice>();
    if (!device->open(config).ok()) return nullptr;
    return device;
  }

private:
  std::string factory_name_;
};

const bool kRv32QemuLogDeviceRegistered = [] {
  auto status = drivers::log::LogDeviceRegistry::instance().registerFactory(
    std::make_unique<Rv32QemuLogDeviceFactory>("rv32qemu"));
  (void)status;
  status = drivers::log::LogDeviceRegistry::instance().registerFactory(
    std::make_unique<Rv32QemuLogDeviceFactory>("rv32qemu-simulator"));
  (void)status;
  return true;
}();

} // namespace
} // namespace audio_studio::platform::simulator
