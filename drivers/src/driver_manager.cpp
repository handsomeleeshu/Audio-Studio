#include "driver_manager.hpp"

#include "autoconfig.h"

#include <utility>

namespace audio_studio::drivers {

DriverManager& DriverManager::instance() {
  static DriverManager manager;
  return manager;
}

framework::Status DriverManager::initialize(const DriverManagerConfig& config) {
  if (initialized_) return framework::Status::success();
  config_ = config;
  auto status = collectRegisteredFactories();
  if (!status.ok()) return status;
  status = createDefaultServices();
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
  return os::OsDriverRegistry::instance();
}

socket::SocketDriverRegistry& DriverManager::socketRegistry() {
  return socket::SocketDriverRegistry::instance();
}

filesystem::FileSystemDriverRegistry& DriverManager::filesystemRegistry() {
  return filesystem::FileSystemDriverRegistry::instance();
}

pipe::PipeDriverRegistry& DriverManager::pipeRegistry() {
  return pipe::PipeDriverRegistry::instance();
}

dynlib::DynlibDriverRegistry& DriverManager::dynlibRegistry() {
  return dynlib::DynlibDriverRegistry::instance();
}

datalink::DataLinkDeviceRegistry& DriverManager::datalinkRegistry() {
  return datalink::DataLinkDeviceRegistry::instance();
}

audio::AudioDeviceRegistry& DriverManager::audioRegistry() {
  return audio::AudioDeviceRegistry::instance();
}

control::ControlDeviceRegistry& DriverManager::controlRegistry() {
  return control::ControlDeviceRegistry::instance();
}

log::LogDeviceRegistry& DriverManager::logRegistry() {
  return log::LogDeviceRegistry::instance();
}

dump::DumpDeviceRegistry& DriverManager::dumpRegistry() {
  return dump::DumpDeviceRegistry::instance();
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

framework::Status DriverManager::collectRegisteredFactories() {
  framework::Status status;
#ifdef CONFIG_DRIVER_OS
  status = rememberFactoryNames("os", osRegistry().factoryNames(), "Registered OS driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_SOCKET
  status = rememberFactoryNames("socket", socketRegistry().factoryNames(), "Registered socket driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_FILESYSTEM
  status = rememberFactoryNames("filesystem", filesystemRegistry().factoryNames(), "Registered filesystem driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_PIPE
  status = rememberFactoryNames("pipe", pipeRegistry().factoryNames(), "Registered pipe driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_DYNLIB
  status = rememberFactoryNames("dynlib", dynlibRegistry().factoryNames(), "Registered dynamic library driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_DATALINK
  status = rememberFactoryNames("datalink", datalinkRegistry().factoryNames(), "Registered data-link device");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_AUDIO
  status = rememberFactoryNames("audio", audioRegistry().playbackFactoryNames(), "Registered audio playback driver");
  if (!status.ok()) return status;
  status = rememberFactoryNames("audio", audioRegistry().captureFactoryNames(), "Registered audio capture driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_CONTROL
  status = rememberFactoryNames("control", controlRegistry().factoryNames(), "Registered control driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_LOG
  status = rememberFactoryNames("log", logRegistry().factoryNames(), "Registered log driver");
  if (!status.ok()) return status;
#endif

#ifdef CONFIG_DRIVER_DUMP
  status = rememberFactoryNames("dump", dumpRegistry().factoryNames(), "Registered dump driver");
  if (!status.ok()) return status;
#endif

  return framework::Status::success();
}

framework::Status DriverManager::createDefaultServices() {
  framework::Status status;
#ifdef CONFIG_DRIVER_OS
  if (config_.enable_os) {
    status = requireFactory(osRegistry().hasFactory(config_.os_factory), "os", config_.os_factory);
    if (!status.ok()) return status;
    os_driver_ = osRegistry().create(config_.os_factory);
    if (!os_driver_) return framework::Status::unavailable("failed to create OS driver: " + config_.os_factory);
    status = activateDriver("os", config_.os_factory, "Configured OS driver");
    if (!status.ok()) return status;
  }
#endif

#ifdef CONFIG_DRIVER_SOCKET
  if (config_.enable_socket) {
    status = requireFactory(socketRegistry().hasFactory(config_.socket_factory), "socket", config_.socket_factory);
    if (!status.ok()) return status;
    socket_driver_ = socketRegistry().create(config_.socket_factory);
    if (!socket_driver_) return framework::Status::unavailable("failed to create socket driver: " + config_.socket_factory);
    status = socket_driver_->initialize();
    if (!status.ok()) return status;
    status = activateDriver("socket", config_.socket_factory, "Configured socket driver");
    if (!status.ok()) return status;
  }
#endif

#ifdef CONFIG_DRIVER_FILESYSTEM
  if (config_.enable_filesystem) {
    status = requireFactory(filesystemRegistry().hasFactory(config_.filesystem_factory), "filesystem", config_.filesystem_factory);
    if (!status.ok()) return status;
    filesystem_driver_ = filesystemRegistry().create(config_.filesystem_factory);
    if (!filesystem_driver_) return framework::Status::unavailable("failed to create filesystem driver: " + config_.filesystem_factory);
    status = activateDriver("filesystem", config_.filesystem_factory, "Configured filesystem driver");
    if (!status.ok()) return status;
  }
#endif

#ifdef CONFIG_DRIVER_PIPE
  if (config_.enable_pipe) {
    status = requireFactory(pipeRegistry().hasFactory(config_.pipe_factory), "pipe", config_.pipe_factory);
    if (!status.ok()) return status;
    pipe_driver_ = pipeRegistry().create(config_.pipe_factory);
    if (!pipe_driver_) return framework::Status::unavailable("failed to create pipe driver: " + config_.pipe_factory);
    status = activateDriver("pipe", config_.pipe_factory, "Configured pipe driver");
    if (!status.ok()) return status;
  }
#endif

#ifdef CONFIG_DRIVER_DYNLIB
  if (config_.enable_dynlib) {
    status = requireFactory(dynlibRegistry().hasFactory(config_.dynlib_factory), "dynlib", config_.dynlib_factory);
    if (!status.ok()) return status;
    dynlib_driver_ = dynlibRegistry().create(config_.dynlib_factory);
    if (!dynlib_driver_) return framework::Status::unavailable("failed to create dynamic library driver: " + config_.dynlib_factory);
    status = activateDriver("dynlib", config_.dynlib_factory, "Configured dynamic library driver");
    if (!status.ok()) return status;
  }
#endif

#ifdef CONFIG_DRIVER_DATALINK
  if (config_.enable_datalink) {
    status = requireFactory(datalinkRegistry().hasFactory(config_.datalink_factory), "datalink", config_.datalink_factory);
    if (!status.ok()) return status;
    status = activateDriver("datalink", config_.datalink_factory, "Configured data-link device");
    if (!status.ok()) return status;
  }
#endif

#ifdef CONFIG_DRIVER_AUDIO
  if (config_.enable_audio) {
    status = requireFactory(audioRegistry().hasPlaybackFactory(config_.audio_factory), "audio playback", config_.audio_factory);
    if (!status.ok()) return status;
    status = requireFactory(audioRegistry().hasCaptureFactory(config_.audio_factory), "audio capture", config_.audio_factory);
    if (!status.ok()) return status;
    status = activateDriver("audio", config_.audio_factory, "Configured audio device driver");
    if (!status.ok()) return status;
  }
#endif

#ifdef CONFIG_DRIVER_CONTROL
  if (config_.enable_control) {
    status = requireFactory(controlRegistry().hasFactory(config_.control_factory), "control", config_.control_factory);
    if (!status.ok()) return status;
    status = activateDriver("control", config_.control_factory, "Configured control device driver");
    if (!status.ok()) return status;
  }
#endif

#ifdef CONFIG_DRIVER_LOG
  if (config_.enable_log) {
    status = requireFactory(logRegistry().hasFactory(config_.log_factory), "log", config_.log_factory);
    if (!status.ok()) return status;
    status = activateDriver("log", config_.log_factory, "Configured log device driver");
    if (!status.ok()) return status;
  }
#endif

#ifdef CONFIG_DRIVER_DUMP
  if (config_.enable_dump) {
    status = requireFactory(dumpRegistry().hasFactory(config_.dump_factory), "dump", config_.dump_factory);
    if (!status.ok()) return status;
    status = activateDriver("dump", config_.dump_factory, "Configured dump device driver");
    if (!status.ok()) return status;
  }
#endif

  return framework::Status::success();
}

framework::Status DriverManager::rememberDefaultDriver(std::string category, std::string name, std::string detail) {
  if (hasDriver(category, name)) return framework::Status::success();
  return registerDriver({std::move(category), std::move(name), std::move(detail), false});
}

framework::Status DriverManager::rememberFactoryNames(const std::string& category, const std::vector<std::string>& names, const std::string& detail) {
  for (const auto& name : names) {
    auto status = rememberDefaultDriver(category, name, detail);
    if (!status.ok()) return status;
  }
  return framework::Status::success();
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
