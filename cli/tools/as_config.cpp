#include <iostream>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>

#include "autoconfig.h"
#include "cli_common.hpp"
#include "config_service.hpp"
#include "driver_manager.hpp"

namespace {

struct ConfigCliOptions {
  bool self_test = false;
  bool list_module_configs = false;
  bool build_tplg = audio_studio::framework::config::kHostSupportsAlsaTplg;
  bool no_tplg = false;
  bool strict = true;
  std::string target;
  std::string input = "config/A2.json";
  std::string output_dir = "out/as_config/a2";
  std::string project_name = "a2";
  std::string alsatplg = "alsatplg";
  std::vector<std::string> plugin_paths;
};

int parseOptions(int argc, char** argv, ConfigCliOptions& options) {
  CLI::App app{"Audio Studio topology/config compiler", "as_config"};
  app.option_defaults()->always_capture_default();
  app.add_flag("--self-test", options.self_test, "Run host-side smoke path");
  app.add_option("--target", options.target, "Host-alone target. Use dummy for local smoke tests");
  app.add_flag("--list-module-configs", options.list_module_configs, "List registered module config handlers");
  app.add_option("--input,-i", options.input, "Input Audio Studio project JSON");
  app.add_option("--out-dir,-o", options.output_dir, "Output directory for generated files");
  app.add_option("--project-name", options.project_name, "Generated artifact basename");
  app.add_option("--alsatplg", options.alsatplg, "alsatplg executable path");
  app.add_flag("--build-tplg", options.build_tplg, "Run alsatplg compile/decode after generating conf on Linux hosts");
  app.add_flag("--no-tplg", options.no_tplg, "Only generate conf and sidecar outputs");
  app.add_flag("--strict", options.strict, "Fail on strict validation errors");
  app.add_option("--plugin", options.plugin_paths, "Module config plugin dynamic library path");
  app.allow_extras(false);
  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& error) {
    return app.exit(error);
  }
  if (options.no_tplg) options.build_tplg = false;
  return -1;
}

std::string jsonField(const std::string& key, const std::string& value) {
  return "\"" + key + "\":\"" + audio_studio::cli::jsonEscape(value) + "\"";
}

void printCompileOutput(const audio_studio::framework::config::ConfigCompileOutput& output) {
  std::cout << "{"
            << "\"ok\":" << (output.ok ? "true" : "false") << ","
            << "\"tplg_built\":" << (output.tplg_built ? "true" : "false") << ","
            << "\"tplg_decoded\":" << (output.tplg_decoded ? "true" : "false") << ","
            << jsonField("tool", "as_config") << ","
            << jsonField("conf_path", output.conf_path) << ","
            << jsonField("tplg_path", output.tplg_path) << ","
            << jsonField("private_bin_path", output.private_bin_path) << ","
            << jsonField("ids_header_path", output.ids_header_path) << ","
            << jsonField("private_header_path", output.private_header_path) << ","
            << jsonField("preset_header_path", output.preset_header_path) << ","
            << jsonField("controls_csv_path", output.controls_csv_path) << ","
            << jsonField("report_path", output.report_path) << ","
            << jsonField("alsatplg_log_path", output.alsatplg_log_path) << ","
            << jsonField("tplg_decode_conf_path", output.tplg_decode_conf_path) << ","
            << jsonField("tplg_decode_log_path", output.tplg_decode_log_path) << ","
            << "\"module_type_count\":" << output.module_type_count << ","
            << "\"module_instance_count\":" << output.module_instance_count << ","
            << "\"pipeline_count\":" << output.pipeline_count << ","
            << "\"runtime_control_count\":" << output.runtime_control_count << ","
            << "\"install_param_count\":" << output.install_param_count << ","
            << "\"preset_count\":" << output.preset_count << ","
            << "\"plugin_count\":" << output.plugin_count
            << "}\n";
}

} // namespace

int main(int argc, char** argv) {
#if !defined(CONFIG_FRAMEWORK_CONFIG)
  (void)argc;
  (void)argv;
  std::cerr << "{\"ok\":false,\"error\":\"as_config was built without CONFIG_FRAMEWORK_CONFIG\"}\n";
  return 2;
#else
  ConfigCliOptions options;
  const int parse_result = parseOptions(argc, argv, options);
  if (parse_result >= 0) return parse_result;

  if (options.self_test || options.target == "dummy") {
    audio_studio::cli::Args args(argc, argv);
    return audio_studio::cli::runDummyTool("as_config", "compile", args);
  }

  auto& manager = audio_studio::drivers::DriverManager::instance();
  audio_studio::drivers::DriverManagerConfig driver_config;
  driver_config.enable_os = true;
  driver_config.enable_filesystem = true;
  driver_config.enable_dynlib = true;
  driver_config.enable_socket = false;
  driver_config.enable_pipe = false;
  driver_config.enable_transport = false;
  driver_config.enable_audio = false;
  driver_config.enable_control = false;
  driver_config.enable_log = false;
  driver_config.enable_dump = false;

  auto status = manager.initialize(driver_config);
  if (!status.ok()) {
    std::cerr << status.toJson() << "\n";
    return 1;
  }

  try {
    audio_studio::framework::config::ConfigService service(&manager.filesystem(), &manager.os(), &manager.dynlib());

    if (options.list_module_configs) {
      std::cout << "{\"ok\":true,\"tool\":\"as_config\",\"module_config_handlers\":[";
      const auto handlers = service.moduleConfigs().handlerIds();
      for (size_t i = 0; i < handlers.size(); ++i) {
        if (i != 0) std::cout << ",";
        std::cout << "\"" << audio_studio::cli::jsonEscape(handlers[i]) << "\"";
      }
      std::cout << "]}\n";
      manager.shutdown();
      return 0;
    }

    audio_studio::framework::config::ConfigCompileRequest request;
    request.input_path = options.input;
    request.output_dir = options.output_dir;
    request.project_name = options.project_name;
    request.alsatplg = options.alsatplg;
    request.build_tplg = options.build_tplg;
    request.strict = options.strict;
    request.plugin_paths = options.plugin_paths;

    audio_studio::framework::config::ConfigCompileOutput output;
    status = service.compile(request, output);
    if (!status.ok()) {
      std::cerr << status.toJson() << "\n";
      manager.shutdown();
      return 1;
    }

    printCompileOutput(output);
    manager.shutdown();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "{\"ok\":false,\"error\":\"" << audio_studio::cli::jsonEscape(error.what()) << "\"}\n";
    manager.shutdown();
    return 1;
  }
#endif
}
