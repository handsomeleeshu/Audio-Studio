#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace audio_studio::module_config {

enum class StatusCode : uint32_t {
  kOk = 0,
  kInvalidArgument,
  kUnavailable,
  kInternal,
};

class Status {
public:
  Status() = default;
  Status(StatusCode code, std::string message)
    : code_(code), message_(std::move(message)) {}

  static Status success() { return {}; }
  static Status invalidArgument(std::string message) {
    return {StatusCode::kInvalidArgument, std::move(message)};
  }
  static Status unavailable(std::string message) {
    return {StatusCode::kUnavailable, std::move(message)};
  }
  static Status internal(std::string message) {
    return {StatusCode::kInternal, std::move(message)};
  }

  bool ok() const { return code_ == StatusCode::kOk; }
  StatusCode code() const { return code_; }
  const std::string& message() const { return message_; }

private:
  StatusCode code_ = StatusCode::kOk;
  std::string message_ = "ok";
};

struct ModuleConfigBlob {
  std::string format = "as-generic-json-v1";
  std::vector<uint8_t> data;
};

struct ModuleConfigRequest {
  std::string module_type;
  std::string pipeline_id;
  std::string node_id;
  std::string instance_id;
  std::string parameter_id;
  std::string preset_id;

  std::string module_type_json;
  std::string module_instance_json;
  std::string pipeline_json;
  std::string node_json;
  std::string parameter_json;
  std::string preset_entry_json;
  std::string values_json;
};

class IModuleConfigHandler {
public:
  virtual ~IModuleConfigHandler() = default;

  virtual std::string id() const = 0;
  virtual std::string moduleType() const = 0;

  virtual Status parseModuleType(const ModuleConfigRequest&) const { return Status::success(); }
  virtual Status validatePreset(const ModuleConfigRequest& request) const = 0;
  virtual Status packPreset(const ModuleConfigRequest& request, ModuleConfigBlob& out) const = 0;
  virtual Status packInstallConfig(const ModuleConfigRequest& request, ModuleConfigBlob& out) const = 0;
  virtual Status packRuntimeParam(const ModuleConfigRequest& request, ModuleConfigBlob& out) const = 0;
};

class IModuleConfigRegistry {
public:
  virtual ~IModuleConfigRegistry() = default;
  virtual Status registerHandler(std::unique_ptr<IModuleConfigHandler> handler) = 0;
};

using RegisterModuleConfigHandlersFn = bool (*)(IModuleConfigRegistry& registry);

inline constexpr const char* kRegisterModuleConfigHandlersSymbol = "audio_studio_register_module_config_handlers_v1";

} // namespace audio_studio::module_config
