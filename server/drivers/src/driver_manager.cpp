#include "driver_manager.hpp"

#include "autoconfig.h"

#include <utility>

#ifdef CONFIG_DRIVER_AUDIO_LINUX_HOST
#include "../audio/src/linux_host_audio_device.hpp"
#endif
#ifdef CONFIG_DRIVER_CONTROL_LINUX_HOST
#include "../control/src/linux_host_control_device.hpp"
#endif
#ifdef CONFIG_DRIVER_DUMP_LINUX_HOST
#include "../dump/src/linux_host_dump_device.hpp"
#endif
#ifdef CONFIG_DRIVER_DYNLIB_LINUX_HOST
#include "../dynlib/src/linux_host_dynlib_driver.hpp"
#endif
#ifdef CONFIG_DRIVER_FILESYSTEM_LINUX_HOST
#include "../filesystem/src/linux_host_filesystem_driver.hpp"
#endif
#ifdef CONFIG_DRIVER_LOG_LINUX_HOST
#include "../log/src/linux_host_log_device.hpp"
#endif
#ifdef CONFIG_DRIVER_OS_LINUX_HOST
#include "../os/src/linux_host_os_driver.hpp"
#endif
#ifdef CONFIG_DRIVER_PIPE_LINUX_HOST
#include "../pipe/src/linux_host_pipe_driver.hpp"
#endif
#ifdef CONFIG_DRIVER_SOCKET_LINUX_HOST
#include "../socket/src/linux_host_socket_driver.hpp"
#endif
#ifdef CONFIG_DRIVER_TRANSPORT_LINUX_HOST
#include "../transport/src/linux_host_transport_driver.hpp"
#endif

namespace audio_studio::drivers {

DriverManager& DriverManager::instance() {
  static DriverManager manager;
  return manager;
}

framework::Status DriverManager::initialize(const DriverManagerConfig& config) {
  if (initialized_) return framework::Status::success();
  config_ = config;
  if (config_.install_linux_host_defaults) {
    auto status = installDefaultFactories();
    if (!status.ok()) return status;
  }
  auto status = createDefaultServices();
  if (!status.ok()) return status;
  initialized_ = true;
  return framework::Status::success();
}

void DriverManager::shutdown() {
  if (socket_driver_) socket_driver_->shutdown();
  dynlib_driver_.reset();
  pipe_driver_.reset();
  filesystem_driver_.reset();
  socket_driver_.reset();
  os_driver_.reset();
  dump_registry_.clear();
  log_registry_.clear();
  control_registry_.clear();
  audio_registry_.clear();
  transport_registry_.clear();
  dynlib_registry_.clear();
  pipe_registry_.clear();
  filesystem_registry_.clear();
  socket_registry_.clear();
  os_registry_.clear();
  drivers_.clear();
  initialized_ = false;
}

bool DriverManager::initialized() const {
  return initialized_;
}

framework::Status DriverManager::registerDriver(DriverInfo info) {
  if (info.category.empty()) return framework::Status::invalidArgument("driver category is empty");
  if (info.name.empty()) return framework::Status::invalidArgument("driver name is empty");
  const auto driver_key = key(info.category, info.name);
  if (drivers_.find(driver_key) != drivers_.end()) return framework::Status::invalidArgument("driver already registered: " + driver_key);
  drivers_.emplace(driver_key, std::move(info));
  return framework::Status::success();
}

framework::Status DriverManager::unregisterDriver(const std::string& category, const std::string& name) {
  const auto erased = drivers_.erase(key(category, name));
  if (erased == 0) return framework::Status::unavailable("driver not found: " + key(category, name));
  return framework::Status::success();
}

framework::Status DriverManager::setActive(const std::string& category, const std::string& name, bool active) {
  auto it = drivers_.find(key(category, name));
  if (it == drivers_.end()) return framework::Status::unavailable("driver not found: " + key(category, name));
  it->second.active = active;
  return framework::Status::success();
}

framework::Status DriverManager::getDriver(const std::string& category, const std::string& name, DriverInfo& out) const {
  const auto it = drivers_.find(key(category, name));
  if (it == drivers_.end()) return framework::Status::unavailable("driver not found: " + key(category, name));
  out = it->second;
  return framework::Status::success();
}

bool DriverManager::hasDriver(const std::string& category, const std::string& name) const {
  return drivers_.find(key(category, name)) != drivers_.end();
}

std::vector<DriverInfo> DriverManager::listDrivers() const {
  std::vector<DriverInfo> out;
  out.reserve(drivers_.size());
  for (const auto& item : drivers_) out.push_back(item.second);
  return out;
}

std::vector<DriverInfo> DriverManager::listByCategory(const std::string& category) const {
  std::vector<DriverInfo> out;
  for (const auto& item : drivers_) {
    if (item.second.category == category) out.push_back(item.second);
  }
  return out;
}

size_t DriverManager::size() const {
  return drivers_.size();
}

os::OsDriverRegistry& DriverManager::osRegistry() {
  return os_registry_;
}

socket::SocketDriverRegistry& DriverManager::socketRegistry() {
  return socket_registry_;
}

filesystem::FileSystemDriverRegistry& DriverManager::filesystemRegistry() {
  return filesystem_registry_;
}

pipe::PipeDriverRegistry& DriverManager::pipeRegistry() {
  return pipe_registry_;
}

dynlib::DynlibDriverRegistry& DriverManager::dynlibRegistry() {
  return dynlib_registry_;
}

transport::TransportDriverRegistry& DriverManager::transportRegistry() {
  return transport_registry_;
}

audio::AudioDeviceRegistry& DriverManager::audioRegistry() {
  return audio_registry_;
}

control::ControlDeviceRegistry& DriverManager::controlRegistry() {
  return control_registry_;
}

log::LogDeviceRegistry& DriverManager::logRegistry() {
  return log_registry_;
}

dump::DumpDeviceRegistry& DriverManager::dumpRegistry() {
  return dump_registry_;
}

os::IOsDriver& DriverManager::os() {
  return *os_driver_;
}

socket::ISocketDriver& DriverManager::socket() {
  return *socket_driver_;
}

filesystem::IFileSystemDriver& DriverManager::filesystem() {
  return *filesystem_driver_;
}

pipe::IPipeDriver& DriverManager::pipe() {
  return *pipe_driver_;
}

dynlib::IDynlibDriver& DriverManager::dynlib() {
  return *dynlib_driver_;
}

framework::Status DriverManager::installDefaultFactories() {
  framework::Status status;
#ifdef CONFIG_DRIVER_OS_LINUX_HOST
  if (!os_registry_.hasFactory("linux-host")) {
    auto status = os_registry_.registerFactory(std::make_unique<os::LinuxHostOsDriverFactory>());
    if (!status.ok()) return status;
  }
  status = rememberDefaultDriver("os", "linux-host", "Linux host OS driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_SOCKET_LINUX_HOST
  if (!socket_registry_.hasFactory("linux-host")) {
    auto status = socket_registry_.registerFactory(std::make_unique<socket::LinuxHostSocketDriverFactory>());
    if (!status.ok()) return status;
  }
  status = rememberDefaultDriver("socket", "linux-host", "Linux host socket driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_FILESYSTEM_LINUX_HOST
  if (!filesystem_registry_.hasFactory("linux-host")) {
    auto status = filesystem_registry_.registerFactory(std::make_unique<filesystem::LinuxHostFileSystemDriverFactory>());
    if (!status.ok()) return status;
  }
  status = rememberDefaultDriver("filesystem", "linux-host", "Linux host filesystem driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_PIPE_LINUX_HOST
  if (!pipe_registry_.hasFactory("linux-host")) {
    auto status = pipe_registry_.registerFactory(std::make_unique<pipe::LinuxHostPipeDriverFactory>());
    if (!status.ok()) return status;
  }
  status = rememberDefaultDriver("pipe", "linux-host", "Linux host pipe driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_DYNLIB_LINUX_HOST
  if (!dynlib_registry_.hasFactory("linux-host")) {
    auto status = dynlib_registry_.registerFactory(std::make_unique<dynlib::LinuxHostDynlibDriverFactory>());
    if (!status.ok()) return status;
  }
  status = rememberDefaultDriver("dynlib", "linux-host", "Linux host dynamic library driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_TRANSPORT_LINUX_HOST
  if (!transport_registry_.hasFactory("linux-host")) {
    auto status = transport_registry_.registerFactory(std::make_unique<transport::LinuxHostTransportDriverFactory>());
    if (!status.ok()) return status;
  }
  status = rememberDefaultDriver("transport", "linux-host", "Linux host transport driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_AUDIO_LINUX_HOST
  if (!audio_registry_.hasPlaybackFactory("linux-host")) {
    auto status = audio_registry_.registerPlaybackFactory(std::make_unique<audio::LinuxHostAudioPlaybackDeviceFactory>());
    if (!status.ok()) return status;
  }
  if (!audio_registry_.hasCaptureFactory("linux-host")) {
    auto status = audio_registry_.registerCaptureFactory(std::make_unique<audio::LinuxHostAudioCaptureDeviceFactory>());
    if (!status.ok()) return status;
  }
  status = rememberDefaultDriver("audio", "linux-host", "Linux host audio device driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_CONTROL_LINUX_HOST
  if (!control_registry_.hasFactory("linux-host")) {
    auto status = control_registry_.registerFactory(std::make_unique<control::LinuxHostControlDeviceFactory>());
    if (!status.ok()) return status;
  }
  status = rememberDefaultDriver("control", "linux-host", "Linux host control device driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_LOG_LINUX_HOST
  if (!log_registry_.hasFactory("linux-host")) {
    auto status = log_registry_.registerFactory(std::make_unique<log::LinuxHostLogDeviceFactory>());
    if (!status.ok()) return status;
  }
  status = rememberDefaultDriver("log", "linux-host", "Linux host log device driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_DUMP_LINUX_HOST
  if (!dump_registry_.hasFactory("linux-host")) {
    auto status = dump_registry_.registerFactory(std::make_unique<dump::LinuxHostDumpDeviceFactory>());
    if (!status.ok()) return status;
  }
  status = rememberDefaultDriver("dump", "linux-host", "Linux host dump device driver");
  if (!status.ok()) return status;
#endif

  return framework::Status::success();
}

framework::Status DriverManager::createDefaultServices() {
  framework::Status status;
#ifdef CONFIG_DRIVER_OS
  status = requireFactory(os_registry_.hasFactory(config_.os_factory), "os", config_.os_factory);
  if (!status.ok()) return status;
  os_driver_ = os_registry_.create(config_.os_factory);
  if (!os_driver_) return framework::Status::unavailable("failed to create OS driver: " + config_.os_factory);
  status = activateDriver("os", config_.os_factory, "Configured OS driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_SOCKET
  status = requireFactory(socket_registry_.hasFactory(config_.socket_factory), "socket", config_.socket_factory);
  if (!status.ok()) return status;
  socket_driver_ = socket_registry_.create(config_.socket_factory);
  if (!socket_driver_) return framework::Status::unavailable("failed to create socket driver: " + config_.socket_factory);
  status = socket_driver_->initialize();
  if (!status.ok()) return status;
  status = activateDriver("socket", config_.socket_factory, "Configured socket driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_FILESYSTEM
  status = requireFactory(filesystem_registry_.hasFactory(config_.filesystem_factory), "filesystem", config_.filesystem_factory);
  if (!status.ok()) return status;
  filesystem_driver_ = filesystem_registry_.create(config_.filesystem_factory);
  if (!filesystem_driver_) return framework::Status::unavailable("failed to create filesystem driver: " + config_.filesystem_factory);
  status = activateDriver("filesystem", config_.filesystem_factory, "Configured filesystem driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_PIPE
  status = requireFactory(pipe_registry_.hasFactory(config_.pipe_factory), "pipe", config_.pipe_factory);
  if (!status.ok()) return status;
  pipe_driver_ = pipe_registry_.create(config_.pipe_factory);
  if (!pipe_driver_) return framework::Status::unavailable("failed to create pipe driver: " + config_.pipe_factory);
  status = activateDriver("pipe", config_.pipe_factory, "Configured pipe driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_DYNLIB
  status = requireFactory(dynlib_registry_.hasFactory(config_.dynlib_factory), "dynlib", config_.dynlib_factory);
  if (!status.ok()) return status;
  dynlib_driver_ = dynlib_registry_.create(config_.dynlib_factory);
  if (!dynlib_driver_) return framework::Status::unavailable("failed to create dynamic library driver: " + config_.dynlib_factory);
  status = activateDriver("dynlib", config_.dynlib_factory, "Configured dynamic library driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_TRANSPORT
  status = requireFactory(transport_registry_.hasFactory(config_.transport_factory), "transport", config_.transport_factory);
  if (!status.ok()) return status;
  status = activateDriver("transport", config_.transport_factory, "Configured transport driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_AUDIO
  status = requireFactory(audio_registry_.hasPlaybackFactory(config_.audio_factory), "audio playback", config_.audio_factory);
  if (!status.ok()) return status;
  status = requireFactory(audio_registry_.hasCaptureFactory(config_.audio_factory), "audio capture", config_.audio_factory);
  if (!status.ok()) return status;
  status = activateDriver("audio", config_.audio_factory, "Configured audio device driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_CONTROL
  status = requireFactory(control_registry_.hasFactory(config_.control_factory), "control", config_.control_factory);
  if (!status.ok()) return status;
  status = activateDriver("control", config_.control_factory, "Configured control device driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_LOG
  status = requireFactory(log_registry_.hasFactory(config_.log_factory), "log", config_.log_factory);
  if (!status.ok()) return status;
  status = activateDriver("log", config_.log_factory, "Configured log device driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_DUMP
  status = requireFactory(dump_registry_.hasFactory(config_.dump_factory), "dump", config_.dump_factory);
  if (!status.ok()) return status;
  status = activateDriver("dump", config_.dump_factory, "Configured dump device driver");
  if (!status.ok()) return status;
#endif

  return framework::Status::success();
}

framework::Status DriverManager::rememberDefaultDriver(std::string category, std::string name, std::string detail) {
  if (hasDriver(category, name)) return framework::Status::success();
  return registerDriver({std::move(category), std::move(name), std::move(detail), false});
}

framework::Status DriverManager::activateDriver(std::string category, std::string name, std::string detail) {
  auto status = rememberDefaultDriver(category, name, std::move(detail));
  if (!status.ok()) return status;
  return setActive(category, name, true);
}

framework::Status DriverManager::requireFactory(bool present, const std::string& category, const std::string& name) const {
  if (present) return framework::Status::success();
  return framework::Status::unavailable("driver factory not registered: " + category + ":" + name);
}

std::string DriverManager::key(const std::string& category, const std::string& name) {
  return category + ":" + name;
}

} // namespace audio_studio::drivers
