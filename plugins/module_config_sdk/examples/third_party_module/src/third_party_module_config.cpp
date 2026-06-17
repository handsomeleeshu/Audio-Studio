#include <memory>
#include <string>
#include <utility>

#include "audio_studio/module_config_plugin.hpp"

namespace {

namespace module_config = audio_studio::module_config;

class ThirdPartyModuleConfig final : public module_config::IModuleConfigHandler {
public:
  std::string id() const override { return "example.third-party-module-config-v1"; }
  std::string moduleType() const override { return "test.third_party"; }

  module_config::Status parseModuleType(const module_config::ModuleConfigRequest& request) const override {
    if (request.module_type_json.find("\"test.third_party\"") == std::string::npos) {
      return module_config::Status::invalidArgument("module_type_json does not describe test.third_party");
    }
    return module_config::Status::success();
  }

  module_config::Status validatePreset(const module_config::ModuleConfigRequest& request) const override {
    return validate(request);
  }

  module_config::Status packPreset(const module_config::ModuleConfigRequest& request,
                                   module_config::ModuleConfigBlob& out) const override {
    return pack(request, out, "example-third-party-preset-v1");
  }

  module_config::Status packInstallConfig(const module_config::ModuleConfigRequest& request,
                                          module_config::ModuleConfigBlob& out) const override {
    return pack(request, out, "example-third-party-install-v1");
  }

  module_config::Status packRuntimeParam(const module_config::ModuleConfigRequest& request,
                                         module_config::ModuleConfigBlob& out) const override {
    return pack(request, out, "example-third-party-runtime-v1");
  }

private:
  module_config::Status validate(const module_config::ModuleConfigRequest& request) const {
    if (request.module_type_json.empty()) return module_config::Status::invalidArgument("missing module_type_json");
    if (request.pipeline_json.empty()) return module_config::Status::invalidArgument("missing pipeline_json");
    if (request.node_json.empty()) return module_config::Status::invalidArgument("missing node_json");
    if (request.values_json.empty()) return module_config::Status::invalidArgument("missing values_json");
    return module_config::Status::success();
  }

  module_config::Status pack(const module_config::ModuleConfigRequest& request,
                             module_config::ModuleConfigBlob& out,
                             std::string format) const {
    auto status = validate(request);
    if (!status.ok()) return status;
    out.format = std::move(format);
    out.data.assign(request.values_json.begin(), request.values_json.end());
    return module_config::Status::success();
  }
};

} // namespace

extern "C" bool audio_studio_register_module_config_handlers_v1(
    audio_studio::module_config::IModuleConfigRegistry& registry) {
  return registry.registerHandler(std::make_unique<ThirdPartyModuleConfig>()).ok();
}
