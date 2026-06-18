#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "audio_studio/module_config_plugin.hpp"

namespace {

namespace module_config = audio_studio::module_config;

std::string makeSlug(std::string module_type) {
  for (auto& ch : module_type) {
    if (ch == '.' || ch == '_') ch = '-';
  }
  return module_type;
}

class BuiltinJsonModuleConfigHandler final : public module_config::IModuleConfigHandler {
public:
  explicit BuiltinJsonModuleConfigHandler(std::string module_type)
    : module_type_(std::move(module_type)), slug_(makeSlug(module_type_)) {}

  std::string id() const override { return "as.builtin." + slug_ + "-module-config-v1"; }
  std::string moduleType() const override { return module_type_; }

  module_config::Status parseModuleType(const module_config::ModuleConfigRequest& request) const override {
    if (request.module_type != module_type_) {
      return module_config::Status::invalidArgument(module_type_ + " module config request type mismatch");
    }
    if (request.module_type_json.find("\"" + module_type_ + "\"") == std::string::npos) {
      return module_config::Status::invalidArgument(module_type_ + " module_type_json does not match");
    }
    return module_config::Status::success();
  }

  module_config::Status validatePreset(const module_config::ModuleConfigRequest& request) const override {
    if (request.module_type != module_type_) {
      return module_config::Status::invalidArgument(module_type_ + " module config request type mismatch");
    }
    if (request.values_json.empty()) {
      return module_config::Status::invalidArgument(module_type_ + " values_json is empty");
    }
    return module_config::Status::success();
  }

  module_config::Status packPreset(const module_config::ModuleConfigRequest& request,
                                   module_config::ModuleConfigBlob& out) const override {
    return pack(request, out, "preset");
  }

  module_config::Status packInstallConfig(const module_config::ModuleConfigRequest& request,
                                          module_config::ModuleConfigBlob& out) const override {
    return pack(request, out, "install");
  }

  module_config::Status packRuntimeParam(const module_config::ModuleConfigRequest& request,
                                         module_config::ModuleConfigBlob& out) const override {
    return pack(request, out, "runtime");
  }

private:
  module_config::Status pack(const module_config::ModuleConfigRequest& request,
                             module_config::ModuleConfigBlob& out,
                             const std::string& phase) const {
    auto status = validatePreset(request);
    if (!status.ok()) return status;
    if (request.module_type_json.empty() || request.pipeline_json.empty() || request.node_json.empty()) {
      return module_config::Status::invalidArgument(module_type_ + " request missing JSON context");
    }
    out.format = "as-builtin-" + slug_ + "-" + phase + "-json-v1";
    out.data.assign(request.values_json.begin(), request.values_json.end());
    return module_config::Status::success();
  }

  std::string module_type_;
  std::string slug_;
};

const std::vector<std::string>& builtinModuleTypes() {
  static const std::vector<std::string> module_types = {
    "graph.copier",
    "mix.mixer",
    "route.mux",
    "route.selector",
    "gain.volume",
    "filter.dcblock",
    "eq.iir",
    "eq.fir",
    "dyn.drc",
    "dyn.multiband_drc",
    "filter.crossover",
    "mix.up_down_mixer",
    "fx.virtual_bass",
    "fx.surround_decoder",
    "fx.virtualizer",
    "fx.loudness",
    "protect.speaker",
    "rate.src",
    "rate.asrc",
    "service.anc_filter",
    "vavs.aec",
    "vavs.ns",
    "vavs.agc",
    "vavs.beamformer",
    "vavs.dereverb",
    "voice.kpb",
  };
  return module_types;
}

} // namespace

extern "C" bool audio_studio_register_module_config_handlers_v1(
    audio_studio::module_config::IModuleConfigRegistry& registry) {
  for (const auto& module_type : builtinModuleTypes()) {
    const auto status = registry.registerHandler(std::make_unique<BuiltinJsonModuleConfigHandler>(module_type));
    if (!status.ok()) return false;
  }
  return true;
}
