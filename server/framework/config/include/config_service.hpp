#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "module_config_plugin.hpp"
#include "dynlib_driver.hpp"
#include "filesystem_driver.hpp"
#include "os_driver.hpp"
#include "status.hpp"

namespace audio_studio::framework::config {

#if defined(__linux__)
inline constexpr bool kHostSupportsAlsaTplg = true;
#else
inline constexpr bool kHostSupportsAlsaTplg = false;
#endif

struct ConfigCompileRequest {
  std::string input_path;
  std::string output_dir;
  std::string project_name = "a2";
  std::string alsatplg = "alsatplg";
  bool build_tplg = kHostSupportsAlsaTplg;
  bool strict = true;
  std::vector<std::string> plugin_paths;
};

struct ConfigCompileOutput {
  bool ok = false;
  bool tplg_built = false;
  bool tplg_decoded = false;
  std::string conf_path;
  std::string tplg_path;
  std::string private_bin_path;
  std::string ids_header_path;
  std::string private_header_path;
  std::string preset_header_path;
  std::string controls_csv_path;
  std::string report_path;
  std::string alsatplg_log_path;
  std::string tplg_decode_conf_path;
  std::string tplg_decode_log_path;
  size_t module_type_count = 0;
  size_t module_instance_count = 0;
  size_t pipeline_count = 0;
  size_t runtime_control_count = 0;
  size_t install_param_count = 0;
  size_t preset_count = 0;
  size_t plugin_count = 0;
  std::vector<std::string> warnings;
};

class ModuleConfigRegistry final : public audio_studio::module_config::IModuleConfigRegistry {
public:
  audio_studio::module_config::Status registerHandler(
      std::unique_ptr<audio_studio::module_config::IModuleConfigHandler> handler) override;
  const audio_studio::module_config::IModuleConfigHandler* find(const std::string& module_type) const;
  const audio_studio::module_config::IModuleConfigHandler* findExact(const std::string& module_type) const;
  std::vector<std::string> handlerIds() const;
  size_t size() const;

private:
  std::vector<std::unique_ptr<audio_studio::module_config::IModuleConfigHandler>> handlers_;
};

class ConfigService {
public:
  ConfigService();
  ConfigService(drivers::filesystem::IFileSystemDriver* filesystem,
                drivers::os::IOsDriver* os,
                drivers::dynlib::IDynlibDriver* dynlib);

  void setDrivers(drivers::filesystem::IFileSystemDriver* filesystem,
                  drivers::os::IOsDriver* os,
                  drivers::dynlib::IDynlibDriver* dynlib);

  ModuleConfigRegistry& moduleConfigs();
  const ModuleConfigRegistry& moduleConfigs() const;

  Status compile(const ConfigCompileRequest& request, ConfigCompileOutput& output);

private:
  drivers::filesystem::IFileSystemDriver* filesystem_ = nullptr;
  drivers::os::IOsDriver* os_ = nullptr;
  drivers::dynlib::IDynlibDriver* dynlib_ = nullptr;
  std::vector<std::unique_ptr<drivers::dynlib::IDynlib>> plugin_libraries_;
  ModuleConfigRegistry module_configs_;
};

} // namespace audio_studio::framework::config
