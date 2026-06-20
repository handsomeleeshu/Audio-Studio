#include <memory>
#include <utility>

#include "module_config_plugin.hpp"

namespace {

namespace module_config = audio_studio::module_config;

class TestModuleConfigHandler final : public module_config::IModuleConfigHandler {
public:
  std::string id() const override { return "test.module-config-handler-v1"; }
  std::string moduleType() const override { return "test.third_party"; }

  module_config::Status parseModuleType(const module_config::ModuleConfigRequest& request) const override {
    return request.module_type_json.find("\"type_id\"") == std::string::npos
             ? module_config::Status::invalidArgument("test handler did not receive module_type_json")
             : module_config::Status::success();
  }

  module_config::Status validatePreset(
      const module_config::ModuleConfigRequest& request) const override {
    return request.values_json.empty()
             ? module_config::Status::invalidArgument("test handler values_json is empty")
             : module_config::Status::success();
  }

  module_config::Status packPreset(
      const module_config::ModuleConfigRequest& request,
      module_config::ModuleConfigBlob& out) const override {
    return pack(request, out, "test-preset-v1");
  }

  module_config::Status packInstallConfig(
      const module_config::ModuleConfigRequest& request,
      module_config::ModuleConfigBlob& out) const override {
    return pack(request, out, "test-install-v1");
  }

  module_config::Status packRuntimeParam(
      const module_config::ModuleConfigRequest& request,
      module_config::ModuleConfigBlob& out) const override {
    return pack(request, out, "test-runtime-v1");
  }

private:
  module_config::Status pack(const module_config::ModuleConfigRequest& request,
                             module_config::ModuleConfigBlob& out,
                             std::string format) const {
    if (request.module_type_json.empty() || request.node_json.empty()) {
      return module_config::Status::invalidArgument("test handler missing JSON context");
    }
    out.format = std::move(format);
    out.data.assign(request.values_json.begin(), request.values_json.end());
    return module_config::Status::success();
  }
};

} // namespace

extern "C" bool audio_studio_register_module_config_handlers_v1(
    audio_studio::module_config::IModuleConfigRegistry& registry) {
  return registry.registerHandler(std::make_unique<TestModuleConfigHandler>()).ok();
}
