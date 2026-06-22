#include "log_device.hpp"
#include "simulator_pipe_datalink_device.hpp"
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

std::string optionString(const drivers::log::LogDeviceConfig& config,
                         const std::string& key,
                         const std::string& fallback = {}) {
  const auto it = config.options.find(key);
  return it == config.options.end() || it->second.empty() ? fallback : it->second;
}

bool hasTransportEndpoint(const drivers::log::LogDeviceConfig& config) {
  return !optionString(config, "endpoint").empty() ||
         !optionString(config, "rx_path").empty() ||
         !optionString(config, "tx_path").empty();
}

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

drivers::datalink::DataLinkDeviceConfig transportConfigFromLogConfig(const drivers::log::LogDeviceConfig& config) {
  drivers::datalink::DataLinkDeviceConfig transport_config;
  transport_config.name = "rv32qemu-log-datalink";
  transport_config.endpoint = optionString(config, "endpoint");
  transport_config.options = config.options;
  return transport_config;
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
    if (!hasTransportEndpoint(config_)) {
      chunks_.push_back({next_sequence_++, sampleLogBytes(config_.source)});
      ++chunks_written_;
      running_ = true;
      return drivers::log::LogResult::success();
    }

    datalink_ = std::make_unique<SimulatorPipeDataLinkDevice>();
    auto status = datalink_->open(transportConfigFromLogConfig(config_));
    if (!status.ok()) return status;

    manager_ = std::make_unique<framework::transport::TransportManager>();
    framework::transport::DataLinkManagerConfig datalink_config;
    datalink_config.ack_timeout_ms = 1000;
    datalink_config.max_retries = 3;
    status = manager_->bindDataLinkDevice(*datalink_, datalink_config);
    if (!status.ok()) return status;
    status = manager_->openChannel(AC_TRANSPORT_CHANNEL_LOG, "log");
    if (!status.ok()) return status;

    framework::transport::TransportFrame response;
    const std::vector<uint8_t> source(config_.source.begin(), config_.source.end());
    status = manager_->sendSync(AC_TRANSPORT_CHANNEL_LOG, AC_TRANSPORT_LOG_OPEN, source, response, 1000);
    if (!status.ok()) return status;
    status = transportPayloadStatus(response);
    if (!status.ok()) return status;
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
      manager_.reset();
    }
    if (datalink_) {
      datalink_->close();
      datalink_.reset();
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
  std::unique_ptr<SimulatorPipeDataLinkDevice> datalink_;
  std::unique_ptr<framework::transport::TransportManager> manager_;
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
