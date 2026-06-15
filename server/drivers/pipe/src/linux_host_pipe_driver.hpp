#pragma once

#include <map>
#include <vector>

#include "pipe_driver.hpp"

namespace audio_studio::drivers::pipe {

class LinuxHostPipeDriver;

class LinuxHostPipeStream final : public IPipeStream {
public:
  LinuxHostPipeStream(LinuxHostPipeDriver& driver, PipeType type);

  DriverResult open(const PipeConfig& config) override;
  DriverResult read(void* buffer, size_t capacity, size_t& read_bytes, uint32_t timeout_ms) override;
  DriverResult write(const void* data, size_t size, size_t& written_bytes, uint32_t timeout_ms) override;
  DriverResult flush() override;
  void close() override;
  bool isOpen() const override;

private:
  LinuxHostPipeDriver& driver_;
  PipeType type_ = PipeType::Fifo;
  std::string open_path_;
};

class LinuxHostPipeDriver final : public IPipeDriver {
public:
  std::unique_ptr<IPipeStream> createPipeStream(PipeType type) override;
  DriverResult createPipe(const PipeEndpoint& endpoint, PipeType type) override;
  DriverResult removePipe(const PipeEndpoint& endpoint, PipeType type) override;
  DriverResult exists(const PipeEndpoint& endpoint, bool& result) override;

private:
  friend class LinuxHostPipeStream;

  struct PipeState {
    PipeType type = PipeType::Fifo;
    std::vector<uint8_t> buffer;
  };

  std::map<std::string, PipeState> pipes_;
};

class LinuxHostPipeDriverFactory final : public IPipeDriverFactory {
public:
  std::string name() const override { return "linux-host"; }
  std::unique_ptr<IPipeDriver> create() const override { return std::make_unique<LinuxHostPipeDriver>(); }
};

} // namespace audio_studio::drivers::pipe
