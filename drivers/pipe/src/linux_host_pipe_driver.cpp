#include "linux_host_pipe_driver.hpp"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <limits>

namespace audio_studio::drivers::pipe {

namespace {

class LinuxHostPipeDriverFactory final : public IPipeDriverFactory {
public:
  std::string name() const override { return "linux-host"; }
  std::unique_ptr<IPipeDriver> create() const override { return std::make_unique<LinuxHostPipeDriver>(); }
};

const bool kLinuxHostPipeDriverRegistered = [] {
  auto status = PipeDriverRegistry::instance().registerFactory(std::make_unique<LinuxHostPipeDriverFactory>());
  (void)status;
  return true;
}();

DriverResult systemError(const std::string& operation, const std::string& path = {}) {
  const auto target = path.empty() ? std::string{} : ": " + path;
  return DriverResult::internal(operation + target + " failed: " + std::strerror(errno));
}

bool isFifoType(PipeType type) {
  return type == PipeType::Fifo || type == PipeType::NamedPipe;
}

} // namespace

LinuxHostPipeStream::LinuxHostPipeStream(LinuxHostPipeDriver& driver, PipeType type) : driver_(driver), type_(type) {}

LinuxHostPipeStream::~LinuxHostPipeStream() {
  close();
}

DriverResult LinuxHostPipeStream::open(const PipeConfig& config) {
  close();
  type_ = config.type;

  if (type_ == PipeType::Anonymous) {
    int fds[2] = {-1, -1};
    if (::pipe(fds) != 0) return systemError("pipe");
    (void)::fcntl(fds[0], F_SETFL, ::fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);
    (void)::fcntl(fds[1], F_SETFL, ::fcntl(fds[1], F_GETFL, 0) | O_NONBLOCK);
    (void)::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    (void)::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    read_fd_ = fds[0];
    write_fd_ = fds[1];
    return DriverResult::success();
  }

  if (!isFifoType(type_)) return DriverResult::invalidArgument("unsupported pipe type");
  if (config.endpoint.path.empty()) return DriverResult::invalidArgument("pipe path is empty");

  struct stat statbuf {};
  if (::stat(config.endpoint.path.c_str(), &statbuf) != 0) return systemError("stat pipe", config.endpoint.path);
  if (!S_ISFIFO(statbuf.st_mode)) return DriverResult::invalidArgument("pipe path is not a FIFO: " + config.endpoint.path);

  const int fd = ::open(config.endpoint.path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) return systemError("open pipe", config.endpoint.path);
  read_fd_ = fd;
  write_fd_ = fd;
  open_path_ = config.endpoint.path;
  return DriverResult::success();
}

DriverResult LinuxHostPipeStream::read(void* buffer, size_t capacity, size_t& read_bytes, uint32_t timeout_ms) {
  read_bytes = 0;
  if (read_fd_ < 0) return DriverResult::unavailable("pipe is not open");
  if (buffer == nullptr && capacity > 0) return DriverResult::invalidArgument("pipe read buffer is null");
  if (capacity == 0) return DriverResult::success();

  auto status = waitFor(read_fd_, POLLIN, timeout_ms);
  if (!status.ok()) return status;

  const auto rc = ::read(read_fd_, buffer, capacity);
  if (rc < 0) return systemError("read pipe", open_path_);
  if (rc == 0) return DriverResult::unavailable("pipe peer closed");
  read_bytes = static_cast<size_t>(rc);
  return DriverResult::success();
}

DriverResult LinuxHostPipeStream::write(const void* data, size_t size, size_t& written_bytes, uint32_t timeout_ms) {
  written_bytes = 0;
  if (write_fd_ < 0) return DriverResult::unavailable("pipe is not open");
  if (data == nullptr && size > 0) return DriverResult::invalidArgument("pipe write buffer is null");
  if (size == 0) return DriverResult::success();

  auto status = waitFor(write_fd_, POLLOUT, timeout_ms);
  if (!status.ok()) return status;

  const auto rc = ::write(write_fd_, data, size);
  if (rc < 0) return systemError("write pipe", open_path_);
  written_bytes = static_cast<size_t>(rc);
  return DriverResult::success();
}

DriverResult LinuxHostPipeStream::flush() {
  if (write_fd_ < 0) return DriverResult::unavailable("pipe is not open");
  return DriverResult::success();
}

void LinuxHostPipeStream::close() {
  if (read_fd_ >= 0) {
    ::close(read_fd_);
    if (write_fd_ == read_fd_) write_fd_ = -1;
    read_fd_ = -1;
  }
  if (write_fd_ >= 0) {
    ::close(write_fd_);
    write_fd_ = -1;
  }
  open_path_.clear();
}

bool LinuxHostPipeStream::isOpen() const {
  return read_fd_ >= 0 || write_fd_ >= 0;
}

DriverResult LinuxHostPipeStream::waitFor(int fd, short events, uint32_t timeout_ms) const {
  pollfd descriptor {};
  descriptor.fd = fd;
  descriptor.events = events;
  const int timeout = timeout_ms == std::numeric_limits<uint32_t>::max() ? -1 : static_cast<int>(timeout_ms);
  const int rc = ::poll(&descriptor, 1, timeout);
  if (rc < 0) return systemError("poll pipe", open_path_);
  if (rc == 0) return DriverResult::unavailable("pipe operation timed out");
  if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) return DriverResult::unavailable("pipe error event");
  if ((descriptor.revents & events) == 0) return DriverResult::unavailable("pipe event not ready");
  return DriverResult::success();
}

std::unique_ptr<IPipeStream> LinuxHostPipeDriver::createPipeStream(PipeType type) {
  return std::make_unique<LinuxHostPipeStream>(*this, type);
}

DriverResult LinuxHostPipeDriver::createPipe(const PipeEndpoint& endpoint, PipeType type) {
  if (!isFifoType(type)) return DriverResult::invalidArgument("createPipe only supports FIFO/named pipe on Linux host");
  if (endpoint.path.empty()) return DriverResult::invalidArgument("pipe path is empty");
  if (::mkfifo(endpoint.path.c_str(), 0666) != 0) {
    if (errno == EEXIST) return DriverResult::invalidArgument("pipe already exists: " + endpoint.path);
    return systemError("mkfifo", endpoint.path);
  }
  return framework::Status::success();
}

DriverResult LinuxHostPipeDriver::removePipe(const PipeEndpoint& endpoint, PipeType type) {
  if (!isFifoType(type)) return DriverResult::invalidArgument("removePipe only supports FIFO/named pipe on Linux host");
  if (endpoint.path.empty()) return DriverResult::invalidArgument("pipe path is empty");
  if (::unlink(endpoint.path.c_str()) != 0) {
    if (errno == ENOENT) return DriverResult::unavailable("pipe not found: " + endpoint.path);
    return systemError("unlink pipe", endpoint.path);
  }
  return framework::Status::success();
}

DriverResult LinuxHostPipeDriver::exists(const PipeEndpoint& endpoint, bool& result) {
  if (endpoint.path.empty()) return DriverResult::invalidArgument("pipe path is empty");
  struct stat statbuf {};
  if (::stat(endpoint.path.c_str(), &statbuf) != 0) {
    if (errno == ENOENT) {
      result = false;
      return DriverResult::success();
    }
    return systemError("stat pipe", endpoint.path);
  }
  result = S_ISFIFO(statbuf.st_mode);
  return framework::Status::success();
}

} // namespace audio_studio::drivers::pipe
