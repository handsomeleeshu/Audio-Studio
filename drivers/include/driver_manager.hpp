#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "audio_device.hpp"
#include "status.hpp"
#include "control_device.hpp"
#include "dump_device.hpp"
#include "dynlib_driver.hpp"
#include "filesystem_driver.hpp"
#include "log_device.hpp"
#include "os_driver.hpp"
#include "pipe_driver.hpp"
#include "socket_driver.hpp"
#include "transport_driver.hpp"

namespace audio_studio::drivers {

struct DriverInfo {
  std::string category;
  std::string name;
  std::string detail;
  bool active = false;
};

struct DriverManagerConfig {
  std::string os_factory = "linux-host";
  std::string socket_factory = "linux-host";
  std::string filesystem_factory = "linux-host";
  std::string pipe_factory = "linux-host";
  std::string dynlib_factory = "linux-host";
  std::string transport_factory = "linux-host";
  std::string audio_factory = "linux-host";
  std::string control_factory = "linux-host";
  std::string log_factory = "linux-host";
  std::string dump_factory = "linux-host";
  bool enable_os = true;
  bool enable_socket = true;
  bool enable_filesystem = true;
  bool enable_pipe = true;
  bool enable_dynlib = true;
  bool enable_transport = true;
  bool enable_audio = true;
  bool enable_control = true;
  bool enable_log = true;
  bool enable_dump = true;
};

class DriverManager {
public:
  static DriverManager& instance();

  framework::Status initialize(const DriverManagerConfig& config = {});
  void shutdown();
  bool initialized() const;

  framework::Status registerDriver(DriverInfo info);
  framework::Status unregisterDriver(const std::string& category, const std::string& name);
  framework::Status setActive(const std::string& category, const std::string& name, bool active);
  framework::Status getDriver(const std::string& category, const std::string& name, DriverInfo& out) const;
  bool hasDriver(const std::string& category, const std::string& name) const;
  std::vector<DriverInfo> listDrivers() const;
  std::vector<DriverInfo> listByCategory(const std::string& category) const;
  size_t size() const;

  os::OsDriverRegistry& osRegistry();
  socket::SocketDriverRegistry& socketRegistry();
  filesystem::FileSystemDriverRegistry& filesystemRegistry();
  pipe::PipeDriverRegistry& pipeRegistry();
  dynlib::DynlibDriverRegistry& dynlibRegistry();
  transport::TransportDriverRegistry& transportRegistry();
  audio::AudioDeviceRegistry& audioRegistry();
  control::ControlDeviceRegistry& controlRegistry();
  log::LogDeviceRegistry& logRegistry();
  dump::DumpDeviceRegistry& dumpRegistry();

  os::IOsDriver& os();
  socket::ISocketDriver& socket();
  filesystem::IFileSystemDriver& filesystem();
  pipe::IPipeDriver& pipe();
  dynlib::IDynlibDriver& dynlib();

private:
  static std::string key(const std::string& category, const std::string& name);

  framework::Status collectRegisteredFactories();
  framework::Status createDefaultServices();
  framework::Status rememberDefaultDriver(std::string category, std::string name, std::string detail);
  framework::Status rememberFactoryNames(const std::string& category, const std::vector<std::string>& names, const std::string& detail);
  framework::Status activateDriver(std::string category, std::string name, std::string detail);
  framework::Status requireFactory(bool present, const std::string& category, const std::string& name) const;

  bool initialized_ = false;
  DriverManagerConfig config_;
  std::map<std::string, DriverInfo> drivers_;

  std::unique_ptr<os::IOsDriver> os_driver_;
  std::unique_ptr<socket::ISocketDriver> socket_driver_;
  std::unique_ptr<filesystem::IFileSystemDriver> filesystem_driver_;
  std::unique_ptr<pipe::IPipeDriver> pipe_driver_;
  std::unique_ptr<dynlib::IDynlibDriver> dynlib_driver_;
};

} // namespace audio_studio::drivers
