#include "log_device.hpp"
#include "simulator_pipe_transport_driver.hpp"
#include "transport_manager.hpp"

#include <fstream>
#include <memory>
#include <sstream>
#include <utility>

namespace audio_studio::platform::simulator {
namespace {

constexpr uint16_t kLogChannelId = 1;
constexpr uint16_t kLogCommandOpen = 1;
constexpr uint16_t kLogCommandRead = 2;

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

drivers::transport::TransportConfig transportConfigFromLogConfig(const drivers::log::LogDeviceConfig& config) {
  drivers::transport::TransportConfig transport_config;
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

    datalink_ = std::make_unique<SimulatorPipeTransportDriver>();
    auto status = datalink_->open(transportConfigFromLogConfig(config_));
    if (!status.ok()) return status;

    manager_ = std::make_unique<framework::transport::TransportManager>();
    status = manager_->bindDataLinkDevice(*datalink_);
    if (!status.ok()) return status;
    status = manager_->openChannel(kLogChannelId, "log");
    if (!status.ok()) return status;

    framework::transport::TransportFrame response;
    const std::vector<uint8_t> source(config_.source.begin(), config_.source.end());
    status = manager_->sendSync(kLogChannelId, kLogCommandOpen, source, response, 1000);
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
      (void)manager_->closeChannel(kLogChannelId);
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
    const auto status = manager_->sendSync(kLogChannelId, kLogCommandRead, {}, response, timeout_ms);
    if (!status.ok()) return status;
    if (response.payload.empty()) return drivers::log::LogResult::unavailable("rv32qemu log read returned no data");
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
  std::unique_ptr<SimulatorPipeTransportDriver> datalink_;
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
