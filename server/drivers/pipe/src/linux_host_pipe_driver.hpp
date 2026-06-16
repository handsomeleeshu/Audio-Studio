#pragma once

#include "pipe_driver.hpp"

namespace audio_studio::drivers::pipe {

class LinuxHostPipeDriver;

class LinuxHostPipeStream final : public IPipeStream {
public:
  LinuxHostPipeStream(LinuxHostPipeDriver& driver, PipeType type);
  ~LinuxHostPipeStream() override;

  DriverResult open(const PipeConfig& config) override;
  DriverResult read(void* buffer, size_t capacity, size_t& read_bytes, uint32_t timeout_ms) override;
  DriverResult write(const void* data, size_t size, size_t& written_bytes, uint32_t timeout_ms) override;
  DriverResult flush() override;
  void close() override;
  bool isOpen() const override;

private:
  DriverResult waitFor(int fd, short events, uint32_t timeout_ms) const;

  LinuxHostPipeDriver& driver_;
  PipeType type_ = PipeType::Fifo;
  std::string open_path_;
  int read_fd_ = -1;
  int write_fd_ = -1;
};

class LinuxHostPipeDriver final : public IPipeDriver {
public:
  std::unique_ptr<IPipeStream> createPipeStream(PipeType type) override;
  DriverResult createPipe(const PipeEndpoint& endpoint, PipeType type) override;
  DriverResult removePipe(const PipeEndpoint& endpoint, PipeType type) override;
  DriverResult exists(const PipeEndpoint& endpoint, bool& result) override;

private:
  friend class LinuxHostPipeStream;
};

} // namespace audio_studio::drivers::pipe
