#include "config_service.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "json_value.hpp"

namespace audio_studio::framework::config {
namespace {

using audio_studio::rpc::JsonValue;
namespace module_config = audio_studio::module_config;

constexpr uint32_t kAsPrivateMagic = 0x46435341u; // ASCF, little endian.
constexpr uint16_t kAsPrivateVersionMajor = 1;
constexpr uint16_t kAsPrivateVersionMinor = 0;
constexpr uint32_t kFnvOffset = 2166136261u;
constexpr uint32_t kFnvPrime = 16777619u;

struct RangeInfo {
  bool present = false;
  int64_t min = 0;
  int64_t max = 0;
  int64_t step = 1;
};

struct ParameterInfo {
  std::string param_id;
  std::string param_name;
  std::string value_type;
  std::string source_json;
  std::string default_json;
  std::string apply_mode;
  std::vector<std::string> states;
  std::vector<std::string> enum_values;
  RangeInfo range;
};

struct ModuleTypeInfo {
  std::string type_id;
  std::string category;
  std::string module_class;
  std::string source_json;
  std::vector<ParameterInfo> parameters;
};

struct InstanceInfo {
  std::string inst_id;
  std::string name;
  std::string module_type;
  std::string source_json;
};

struct NodeInfo {
  std::string node_id;
  std::string kind;
  std::string inst_id;
  std::string name;
  std::string module_type;
  std::string widget_name;
  std::string source_json;
};

struct EdgeInfo {
  std::string from_node;
  std::string to_node;
};

struct PortInfo {
  std::string port_id;
  std::string role;
  std::string pcm_id;
  std::string dai_id;
  std::string aif_role;
  std::string transport;
  std::string hw_id;
  std::string hw_dir;
  uint32_t max_ch = 0;
  uint32_t tdm_slots = 0;
  uint32_t slot_width = 0;
  uint32_t sample_bits = 0;
  uint32_t fsync_hz = 0;
};

struct PipelineInfo {
  std::string pipe_id;
  std::string name;
  std::string domain;
  std::string direction;
  std::string source_json;
  uint32_t pcm_index = 0;
  uint32_t sample_rate = 48000;
  uint32_t channels_min = 1;
  uint32_t channels_max = 2;
  std::vector<PortInfo> ports;
  std::vector<NodeInfo> nodes;
  std::vector<EdgeInfo> edges;
};

struct ControlInfo {
  std::string pipe_id;
  std::string node_id;
  std::string inst_id;
  std::string module_type;
  std::string param_id;
  std::string param_name;
  std::string value_type;
  std::string control_name;
  std::string macro_name;
  uint32_t type_uid = 0;
  uint32_t param_uid = 0;
  uint32_t control_uid = 0;
  RangeInfo range;
  std::vector<std::string> enum_values;
  std::string config_format;
  std::vector<uint8_t> default_payload;
};

struct InstallParamInfo {
  std::string pipe_id;
  std::string node_id;
  std::string inst_id;
  std::string module_type;
  std::string param_id;
  uint32_t param_uid = 0;
  std::string config_format;
  std::vector<uint8_t> payload;
};

struct PresetEntryInfo {
  std::string preset_id;
  std::string pipeline_id;
  std::string node_id;
  std::string inst_id;
  std::string module_type;
  std::string source_json;
  std::string config_format;
  std::vector<uint8_t> payload;
};

struct PresetInfo {
  std::string preset_id;
  std::string description;
  std::string load_mode;
  bool validate_all_before_apply = false;
  bool rollback_on_error = false;
  std::string apply_order = "as_listed";
  uint32_t preset_uid = 0;
  std::vector<PresetEntryInfo> entries;
};

struct ProjectIr {
  std::string project_name = "a2";
  std::map<std::string, ModuleTypeInfo> module_types;
  std::map<std::string, InstanceInfo> instances;
  std::vector<PipelineInfo> pipelines;
  std::vector<ControlInfo> controls;
  std::vector<InstallParamInfo> install_params;
  std::vector<PresetInfo> presets;
  std::vector<std::string> warnings;
};

class GenericModuleConfigHandler final : public module_config::IModuleConfigHandler {
public:
  std::string id() const override { return "as.generic-module-config-json-v1"; }
  std::string moduleType() const override { return "*"; }

  module_config::Status validatePreset(const module_config::ModuleConfigRequest& request) const override {
    if (request.module_type.empty()) return module_config::Status::invalidArgument("module config request module_type is empty");
    if (request.values_json.empty()) return module_config::Status::invalidArgument("module config request values_json is empty");
    return module_config::Status::success();
  }

  module_config::Status packPreset(const module_config::ModuleConfigRequest& request,
                                   module_config::ModuleConfigBlob& out) const override {
    return packJson(request, out, "as-generic-preset-json-v1");
  }

  module_config::Status packInstallConfig(const module_config::ModuleConfigRequest& request,
                                          module_config::ModuleConfigBlob& out) const override {
    return packJson(request, out, "as-generic-install-json-v1");
  }

  module_config::Status packRuntimeParam(const module_config::ModuleConfigRequest& request,
                                         module_config::ModuleConfigBlob& out) const override {
    return packJson(request, out, "as-generic-runtime-json-v1");
  }

private:
  module_config::Status packJson(const module_config::ModuleConfigRequest& request,
                                 module_config::ModuleConfigBlob& out,
                                 std::string format) const {
    auto status = validatePreset(request);
    if (!status.ok()) return status;
    out.format = std::move(format);
    out.data.assign(request.values_json.begin(), request.values_json.end());
    return module_config::Status::success();
  }
};

uint32_t fnv1a32(const std::string& input) {
  uint32_t hash = kFnvOffset;
  for (const unsigned char ch : input) {
    hash ^= ch;
    hash *= kFnvPrime;
  }
  if (hash == 0) hash = 1;
  return hash;
}

uint32_t stableUid(const std::string& domain, const std::string& identity) {
  return fnv1a32("as:" + domain + ":v1:" + identity);
}

std::string hex32(uint32_t value) {
  std::ostringstream out;
  out << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << value;
  return out.str();
}

std::string csvEscape(const std::string& value) {
  bool quote = false;
  for (const char ch : value) {
    if (ch == ',' || ch == '"' || ch == '\n' || ch == '\r') quote = true;
  }
  if (!quote) return value;
  std::string out = "\"";
  for (const char ch : value) {
    if (ch == '"') out += "\"\"";
    else out.push_back(ch);
  }
  out.push_back('"');
  return out;
}

std::string macroToken(const std::string& input) {
  std::string out;
  out.reserve(input.size() + 4);
  for (const unsigned char ch : input) {
    if (std::isalnum(ch)) out.push_back(static_cast<char>(std::toupper(ch)));
    else out.push_back('_');
  }
  while (out.find("__") != std::string::npos) {
    const auto pos = out.find("__");
    out.replace(pos, 2, "_");
  }
  while (!out.empty() && out.front() == '_') out.erase(out.begin());
  while (!out.empty() && out.back() == '_') out.pop_back();
  if (out.empty()) out = "ID";
  if (std::isdigit(static_cast<unsigned char>(out.front()))) out.insert(out.begin(), '_');
  return out;
}

std::string stringValue(const JsonValue& object, const std::string& key, const std::string& fallback = {}) {
  if (!object.isObject() || !object.has(key) || object.at(key).isNull()) return fallback;
  if (!object.at(key).isString()) throw std::runtime_error("JSON field must be string: " + key);
  return object.at(key).asString();
}

uint32_t uintValue(const JsonValue& object, const std::string& key, uint32_t fallback) {
  if (!object.isObject() || !object.has(key) || object.at(key).isNull()) return fallback;
  if (!object.at(key).isNumber()) throw std::runtime_error("JSON field must be number: " + key);
  return static_cast<uint32_t>(object.at(key).asUInt64());
}

bool boolValue(const JsonValue& object, const std::string& key, bool fallback) {
  if (!object.isObject() || !object.has(key) || object.at(key).isNull()) return fallback;
  if (!object.at(key).isBool()) throw std::runtime_error("JSON field must be bool: " + key);
  return object.at(key).asBool();
}

const JsonValue& requiredArray(const JsonValue& object, const std::string& key) {
  if (!object.isObject() || !object.has(key) || !object.at(key).isArray()) {
    throw std::runtime_error("JSON field must be array: " + key);
  }
  return object.at(key);
}

const JsonValue& requiredObject(const JsonValue& object, const std::string& key) {
  if (!object.isObject() || !object.has(key) || !object.at(key).isObject()) {
    throw std::runtime_error("JSON field must be object: " + key);
  }
  return object.at(key);
}

std::vector<std::string> stringArray(const JsonValue& object, const std::string& key) {
  std::vector<std::string> out;
  if (!object.isObject() || !object.has(key) || object.at(key).isNull()) return out;
  if (!object.at(key).isArray()) throw std::runtime_error("JSON field must be array: " + key);
  for (const auto& item : object.at(key).asArray()) {
    if (!item.isString()) throw std::runtime_error("JSON array item must be string: " + key);
    out.push_back(item.asString());
  }
  return out;
}

std::vector<std::string> enumArray(const JsonValue& object, const std::string& key) {
  std::vector<std::string> out;
  if (!object.isObject() || !object.has(key) || object.at(key).isNull()) return out;
  if (!object.at(key).isArray()) throw std::runtime_error("JSON field must be array: " + key);
  for (const auto& item : object.at(key).asArray()) {
    if (item.isString()) {
      out.push_back(item.asString());
    } else if (item.isObject()) {
      if (item.has("label") && item.at("label").isString()) out.push_back(item.at("label").asString());
      else if (item.has("name") && item.at("name").isString()) out.push_back(item.at("name").asString());
      else if (item.has("id") && item.at("id").isString()) out.push_back(item.at("id").asString());
      else if (item.has("value") && item.at("value").isNumber()) out.push_back(std::to_string(item.at("value").asInt64()));
      else throw std::runtime_error("enum object must contain label, name, id, or value");
    } else {
      throw std::runtime_error("enum item must be string or object");
    }
  }
  return out;
}

bool containsString(const std::vector<std::string>& values, const std::string& wanted) {
  return std::find(values.begin(), values.end(), wanted) != values.end();
}

RangeInfo parseRange(const JsonValue& param) {
  RangeInfo range;
  if (!param.isObject() || !param.has("range") || param.at("range").isNull()) return range;
  const auto& object = requiredObject(param, "range");
  range.present = true;
  range.min = object.has("min") ? object.at("min").asInt64() : 0;
  range.max = object.has("max") ? object.at("max").asInt64() : 0;
  range.step = object.has("step") ? object.at("step").asInt64() : 1;
  if (range.step <= 0) range.step = 1;
  if (range.max < range.min) std::swap(range.max, range.min);
  return range;
}

std::string defaultJson(const JsonValue& param) {
  if (!param.isObject() || !param.has("default")) return "null";
  return param.at("default").dump();
}

std::string quote(const std::string& value) {
  return audio_studio::rpc::escapeJsonString(value);
}

std::string topologyQuote(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 2);
  out.push_back('"');
  for (const char ch : value) {
    if (ch == '"' || ch == '\\') out.push_back('\\');
    if (ch == '\n' || ch == '\r') out.push_back(' ');
    else out.push_back(ch);
  }
  out.push_back('"');
  return out;
}

std::string shellQuote(const std::string& value) {
  std::string out = "'";
  for (const char ch : value) {
    if (ch == '\'') out += "'\\''";
    else out.push_back(ch);
  }
  out.push_back('\'');
  return out;
}

std::string hexBytes(const std::vector<uint8_t>& bytes) {
  std::ostringstream out;
  for (size_t i = 0; i < bytes.size(); ++i) {
    if (i != 0) out << ",";
    out << "0x" << std::hex << std::nouppercase << std::setw(2) << std::setfill('0')
        << static_cast<unsigned>(bytes[i]);
  }
  return out.str();
}

std::string compactHex(const std::vector<uint8_t>& bytes) {
  std::ostringstream out;
  for (const auto byte : bytes) {
    out << std::hex << std::nouppercase << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
  }
  return out.str();
}

void appendLe16(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xff));
  out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

void appendLe32(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xff));
  out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
  out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
  out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
}

uint32_t crc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0u - (crc & 1u);
      crc = (crc >> 1) ^ (0xedb88320u & mask);
    }
  }
  return ~crc;
}

Status readText(drivers::filesystem::IFileSystemDriver& fs, const std::string& path, std::string& out) {
  auto file = fs.createFile();
  if (!file) return Status::internal("failed to create file object");
  drivers::filesystem::FileOpenOptions options;
  options.read = true;
  options.write = false;
  options.create = false;
  options.truncate = false;
  auto status = file->open(path, options);
  if (!status.ok()) return status;
  const uint64_t file_size = file->size();
  out.clear();
  out.resize(static_cast<size_t>(file_size));
  size_t read_bytes = 0;
  if (file_size > 0) {
    status = file->read(&out[0], out.size(), read_bytes);
    if (!status.ok()) return status;
    out.resize(read_bytes);
  }
  file->close();
  return Status::success();
}

Status writeBytes(drivers::filesystem::IFileSystemDriver& fs, const std::string& path, const std::vector<uint8_t>& bytes) {
  auto file = fs.createFile();
  if (!file) return Status::internal("failed to create file object");
  drivers::filesystem::FileOpenOptions options;
  options.read = false;
  options.write = true;
  options.create = true;
  options.truncate = true;
  auto status = file->open(path, options);
  if (!status.ok()) return status;
  size_t written = 0;
  status = file->write(bytes.data(), bytes.size(), written);
  if (!status.ok()) return status;
  if (written != bytes.size()) return Status::internal("short write: " + path);
  status = file->flush();
  file->close();
  return status;
}

Status writeText(drivers::filesystem::IFileSystemDriver& fs, const std::string& path, const std::string& text) {
  std::vector<uint8_t> bytes(text.begin(), text.end());
  return writeBytes(fs, path, bytes);
}

bool containsAlsaTopologyError(const std::string& log) {
  return log.find("ALSA lib") != std::string::npos ||
         log.find("undefined source widget") != std::string::npos ||
         log.find("undefined sink widget") != std::string::npos ||
         log.find("failed to") != std::string::npos ||
         log.find("Invalid argument") != std::string::npos;
}

Status fromModuleConfigStatus(const module_config::Status& status) {
  if (status.ok()) return Status::success();
  switch (status.code()) {
    case module_config::StatusCode::kInvalidArgument: return Status::invalidArgument(status.message());
    case module_config::StatusCode::kUnavailable: return Status::unavailable(status.message());
    case module_config::StatusCode::kInternal: return Status::internal(status.message());
    case module_config::StatusCode::kOk: return Status::success();
  }
  return Status::internal(status.message());
}

std::string pathJoin(drivers::filesystem::IFileSystemDriver& fs, const std::string& a, const std::string& b) {
  return fs.joinPath({a, b});
}

std::string pathJoin(drivers::filesystem::IFileSystemDriver& fs,
                     const std::string& a,
                     const std::string& b,
                     const std::string& c) {
  return fs.joinPath({a, b, c});
}

std::string controlName(const PipelineInfo& pipeline, const NodeInfo& node, const ParameterInfo& param) {
  return pipeline.pipe_id + " " + node.node_id + " " + param.param_name;
}

std::string graphNodeFromEndpoint(const std::string& endpoint) {
  const auto colon = endpoint.find(':');
  return colon == std::string::npos ? endpoint : endpoint.substr(0, colon);
}

std::string directionForPipeline(const JsonValue& pipeline) {
  const std::string domain = stringValue(pipeline, "domain");
  if (domain.find("capture") != std::string::npos || domain.find("radio") != std::string::npos) return "capture";
  return "playback";
}

uint32_t maxChannelsFromPorts(const JsonValue& pipeline) {
  uint32_t max_channels = 2;
  if (!pipeline.isObject() || !pipeline.has("ports") || !pipeline.at("ports").isArray()) return max_channels;
  for (const auto& port : pipeline.at("ports").asArray()) {
    if (!port.isObject() || !port.has("hw") || !port.at("hw").isObject()) continue;
    max_channels = std::max(max_channels, uintValue(port.at("hw"), "max_ch", max_channels));
  }
  return max_channels;
}

PortInfo parsePort(const JsonValue& port_json, uint32_t default_rate) {
  PortInfo port;
  port.port_id = stringValue(port_json, "port_id");
  port.role = stringValue(port_json, "role");
  if (port.port_id.empty()) throw std::runtime_error("pipeline port missing port_id");
  if (port_json.has("alsa_hint") && port_json.at("alsa_hint").isObject()) {
    const auto& hint = port_json.at("alsa_hint");
    port.pcm_id = stringValue(hint, "pcm_id");
    port.dai_id = stringValue(hint, "dai_id");
    port.aif_role = stringValue(hint, "aif_role");
  }
  if (port_json.has("hw") && port_json.at("hw").isObject()) {
    const auto& hw = port_json.at("hw");
    port.transport = stringValue(hw, "transport");
    port.hw_id = stringValue(hw, "id");
    port.hw_dir = stringValue(hw, "dir");
    port.max_ch = uintValue(hw, "max_ch", 0);
    port.tdm_slots = uintValue(hw, "tdm_slots", port.max_ch);
    port.slot_width = uintValue(hw, "slot_width", 32);
    port.sample_bits = uintValue(hw, "sample_bits", 32);
    port.fsync_hz = uintValue(hw, "fsync_hz", default_rate);
  }
  return port;
}

ModuleTypeInfo parseModuleType(const JsonValue& object) {
  ModuleTypeInfo type;
  type.type_id = stringValue(object, "type_id");
  if (type.type_id.empty()) throw std::runtime_error("module type missing type_id");
  type.source_json = object.dump();
  type.category = stringValue(object, "category");
  type.module_class = stringValue(object, "module_class");
  if (object.has("parameters")) {
    for (const auto& item : requiredArray(object, "parameters").asArray()) {
      ParameterInfo param;
      param.param_id = stringValue(item, "param_id");
      if (param.param_id.empty()) throw std::runtime_error("parameter missing param_id in " + type.type_id);
      param.param_name = stringValue(item, "param_name", param.param_id);
      param.value_type = stringValue(item, "value_type", "bytes");
      param.source_json = item.dump();
      param.default_json = defaultJson(item);
      param.range = parseRange(item);
      if (item.has("enum")) param.enum_values = enumArray(item, "enum");
      if (item.has("apply") && item.at("apply").isObject()) {
        const auto& apply = item.at("apply");
        param.apply_mode = stringValue(apply, "mode");
        param.states = stringArray(apply, "settable_states");
      }
      type.parameters.push_back(std::move(param));
    }
  }
  return type;
}

ProjectIr parseProject(const JsonValue& root, const std::string& project_name) {
  ProjectIr ir;
  ir.project_name = project_name.empty() ? "a2" : project_name;

  for (const auto& item : requiredArray(root, "module_types").asArray()) {
    auto type = parseModuleType(item);
    if (!ir.module_types.emplace(type.type_id, std::move(type)).second) {
      throw std::runtime_error("duplicate module type");
    }
  }

  for (const auto& item : requiredArray(root, "module_instances").asArray()) {
    InstanceInfo inst;
    inst.inst_id = stringValue(item, "inst_id");
    inst.name = stringValue(item, "name", inst.inst_id);
    inst.module_type = stringValue(item, "module_type");
    inst.source_json = item.dump();
    if (inst.inst_id.empty() || inst.module_type.empty()) throw std::runtime_error("module instance is missing inst_id or module_type");
    if (ir.module_types.find(inst.module_type) == ir.module_types.end()) {
      throw std::runtime_error("module instance references unknown module_type: " + inst.module_type);
    }
    if (!ir.instances.emplace(inst.inst_id, std::move(inst)).second) {
      throw std::runtime_error("duplicate module instance");
    }
  }

  uint32_t pcm_index = 0;
  for (const auto& item : requiredArray(root, "pipelines").asArray()) {
    PipelineInfo pipe;
    pipe.pipe_id = stringValue(item, "pipe_id");
    pipe.name = stringValue(item, "name", pipe.pipe_id);
    pipe.domain = stringValue(item, "domain");
    pipe.direction = directionForPipeline(item);
    pipe.source_json = item.dump();
    pipe.pcm_index = pcm_index++;
    pipe.channels_max = maxChannelsFromPorts(item);
    if (item.has("frame") && item.at("frame").isObject()) pipe.sample_rate = uintValue(item.at("frame"), "rate", 48000);

    if (item.has("ports") && item.at("ports").isArray()) {
      for (const auto& port_json : item.at("ports").asArray()) {
        pipe.ports.push_back(parsePort(port_json, pipe.sample_rate));
      }
    }

    for (const auto& node_json : requiredArray(item, "nodes").asArray()) {
      NodeInfo node;
      node.node_id = stringValue(node_json, "node_id");
      node.kind = stringValue(node_json, "kind");
      node.source_json = node_json.dump();
      if (node.node_id.empty()) throw std::runtime_error("pipeline node missing node_id in " + pipe.pipe_id);
      if (node.kind == "module") {
        node.inst_id = stringValue(node_json, "inst_ref");
        const auto inst_it = ir.instances.find(node.inst_id);
        if (inst_it == ir.instances.end()) throw std::runtime_error("node references unknown inst_ref: " + node.inst_id);
        node.name = inst_it->second.name;
        node.module_type = inst_it->second.module_type;
      } else if (node.kind == "module_inline") {
        const auto& inline_object = requiredObject(node_json, "inline");
        node.inst_id = pipe.pipe_id + "." + node.node_id + ".__inline";
        node.name = stringValue(inline_object, "name", node.node_id);
        node.module_type = stringValue(inline_object, "module_type");
        if (ir.module_types.find(node.module_type) == ir.module_types.end()) {
          throw std::runtime_error("inline node references unknown module_type: " + node.module_type);
        }
      } else {
        node.inst_id = stringValue(node_json, "port_ref", node.node_id);
        node.name = node.node_id;
      }
      node.widget_name = pipe.pipe_id + "." + node.node_id;
      pipe.nodes.push_back(std::move(node));
    }

    if (item.has("edges")) {
      for (const auto& edge_json : requiredArray(item, "edges").asArray()) {
        EdgeInfo edge;
        edge.from_node = graphNodeFromEndpoint(stringValue(edge_json, "from"));
        edge.to_node = graphNodeFromEndpoint(stringValue(edge_json, "to"));
        if (!edge.from_node.empty() && !edge.to_node.empty()) pipe.edges.push_back(std::move(edge));
      }
    }

    ir.pipelines.push_back(std::move(pipe));
  }

  return ir;
}

const NodeInfo* findNode(const PipelineInfo& pipeline, const std::string& node_id) {
  for (const auto& node : pipeline.nodes) {
    if (node.node_id == node_id) return &node;
  }
  return nullptr;
}

const ModuleTypeInfo& moduleTypeForNode(const ProjectIr& ir, const NodeInfo& node) {
  const auto it = ir.module_types.find(node.module_type);
  if (it == ir.module_types.end()) throw std::runtime_error("node module type not found: " + node.module_type);
  return it->second;
}

const InstanceInfo* instanceForNode(const ProjectIr& ir, const NodeInfo& node) {
  const auto it = ir.instances.find(node.inst_id);
  return it == ir.instances.end() ? nullptr : &it->second;
}

module_config::ModuleConfigRequest makeModuleConfigRequest(const ProjectIr& ir,
                                                           const PipelineInfo& pipe,
                                                           const NodeInfo& node,
                                                           const ParameterInfo* param,
                                                           std::string values_json,
                                                           std::string preset_id = {},
                                                           std::string preset_entry_json = {}) {
  const auto& type = moduleTypeForNode(ir, node);
  const auto* instance = instanceForNode(ir, node);

  module_config::ModuleConfigRequest request;
  request.module_type = node.module_type;
  request.pipeline_id = pipe.pipe_id;
  request.node_id = node.node_id;
  request.instance_id = node.inst_id;
  request.parameter_id = param == nullptr ? std::string{} : param->param_id;
  request.preset_id = std::move(preset_id);
  request.module_type_json = type.source_json;
  request.module_instance_json = instance == nullptr ? std::string{} : instance->source_json;
  request.pipeline_json = pipe.source_json;
  request.node_json = node.source_json;
  request.parameter_json = param == nullptr ? std::string{} : param->source_json;
  request.preset_entry_json = std::move(preset_entry_json);
  request.values_json = std::move(values_json);
  return request;
}

Status parseModuleTypesWithHandlers(const ProjectIr& ir, const ModuleConfigRegistry& module_configs) {
  for (const auto& item : ir.module_types) {
    const auto& type = item.second;
    const auto* handler = module_configs.findExact(type.type_id);
    if (handler == nullptr) continue;

    module_config::ModuleConfigRequest request;
    request.module_type = type.type_id;
    request.module_type_json = type.source_json;
    auto status = handler->parseModuleType(request);
    if (!status.ok()) return fromModuleConfigStatus(status);
  }
  return Status::success();
}

void addCheckedUid(std::map<uint32_t, std::string>& used, uint32_t uid, const std::string& identity) {
  const auto [it, inserted] = used.emplace(uid, identity);
  if (!inserted && it->second != identity) {
    throw std::runtime_error("stable id collision: " + identity + " collides with " + it->second + " at " + hex32(uid));
  }
}

std::string singleParamValuesJson(const ParameterInfo& param) {
  return "{" + quote(param.param_id) + ":" + param.default_json + "}";
}

Status packParameterDefault(const ProjectIr& ir,
                            const ModuleConfigRegistry& module_configs,
                            const PipelineInfo& pipe,
                            const NodeInfo& node,
                            const ParameterInfo& param,
                            bool runtime,
                            module_config::ModuleConfigBlob& blob) {
  const auto* handler = module_configs.find(node.module_type);
  if (handler == nullptr) return Status::unavailable("no module config handler registered for module_type: " + node.module_type);

  auto request = makeModuleConfigRequest(ir, pipe, node, &param, singleParamValuesJson(param));
  auto status = runtime ? handler->packRuntimeParam(request, blob) : handler->packInstallConfig(request, blob);
  return fromModuleConfigStatus(status);
}

Status buildParameterTables(ProjectIr& ir, const ModuleConfigRegistry& module_configs) {
  std::map<uint32_t, std::string> used;
  for (const auto& type_item : ir.module_types) {
    const auto& type = type_item.second;
    addCheckedUid(used, stableUid("type", type.type_id), "type:" + type.type_id);
    for (const auto& param : type.parameters) {
      const auto param_identity = type.type_id + ":" + param.param_id;
      addCheckedUid(used, stableUid("param", param_identity), "param:" + param_identity);
    }
  }

  for (const auto& pipe : ir.pipelines) {
    for (const auto& node : pipe.nodes) {
      if (node.module_type.empty()) continue;
      const auto& type = moduleTypeForNode(ir, node);
      for (const auto& param : type.parameters) {
        if (containsString(param.states, "RUNNING")) {
          ControlInfo control;
          control.pipe_id = pipe.pipe_id;
          control.node_id = node.node_id;
          control.inst_id = node.inst_id;
          control.module_type = node.module_type;
          control.param_id = param.param_id;
          control.param_name = param.param_name;
          control.value_type = param.value_type;
          control.control_name = controlName(pipe, node, param);
          control.type_uid = stableUid("type", control.module_type);
          control.param_uid = stableUid("param", control.module_type + ":" + control.param_id);
          control.control_uid = stableUid("control", control.pipe_id + ":" + control.node_id + ":" + control.module_type + ":" + control.param_id);
          control.range = param.range;
          control.enum_values = param.enum_values;
          control.macro_name = "AS_CONTROL_" + macroToken(control.pipe_id + "_" + control.node_id + "_" + control.param_id);
          module_config::ModuleConfigBlob blob;
          auto status = packParameterDefault(ir, module_configs, pipe, node, param, true, blob);
          if (!status.ok()) return status;
          control.config_format = blob.format;
          control.default_payload = std::move(blob.data);
          addCheckedUid(used, control.control_uid, "control:" + control.pipe_id + ":" + control.node_id + ":" + control.param_id);
          ir.controls.push_back(std::move(control));
        } else {
          InstallParamInfo install;
          install.pipe_id = pipe.pipe_id;
          install.node_id = node.node_id;
          install.inst_id = node.inst_id;
          install.module_type = node.module_type;
          install.param_id = param.param_id;
          install.param_uid = stableUid("param", install.module_type + ":" + install.param_id);
          module_config::ModuleConfigBlob blob;
          auto status = packParameterDefault(ir, module_configs, pipe, node, param, false, blob);
          if (!status.ok()) return status;
          install.config_format = blob.format;
          install.payload = std::move(blob.data);
          ir.install_params.push_back(std::move(install));
        }
      }
    }
  }
  return Status::success();
}

Status buildPresets(const JsonValue& root, ProjectIr& ir, const ModuleConfigRegistry& module_configs) {
  if (!root.isObject() || !root.has("presets")) return Status::success();
  for (const auto& preset_json : requiredArray(root, "presets").asArray()) {
    PresetInfo preset;
    preset.preset_id = stringValue(preset_json, "preset_id");
    preset.description = stringValue(preset_json, "description");
    preset.load_mode = stringValue(preset_json, "load_mode", "bulk");
    if (preset.preset_id.empty()) return Status::invalidArgument("preset missing preset_id");
    preset.preset_uid = stableUid("preset", preset.preset_id);
    if (preset_json.has("transaction") && preset_json.at("transaction").isObject()) {
      const auto& transaction = preset_json.at("transaction");
      preset.validate_all_before_apply = boolValue(transaction, "validate_all_before_apply", false);
      preset.rollback_on_error = boolValue(transaction, "rollback_on_error", false);
      preset.apply_order = stringValue(transaction, "apply_order", "as_listed");
    }

    for (const auto& entry_json : requiredArray(preset_json, "node_values").asArray()) {
      const std::string pipeline_id = stringValue(entry_json, "pipeline_id");
      const std::string node_id = stringValue(entry_json, "node_id");
      const PipelineInfo* pipeline = nullptr;
      for (const auto& item : ir.pipelines) {
        if (item.pipe_id == pipeline_id) {
          pipeline = &item;
          break;
        }
      }
      if (pipeline == nullptr) return Status::invalidArgument("preset references unknown pipeline: " + pipeline_id);
      const NodeInfo* node = findNode(*pipeline, node_id);
      if (node == nullptr) return Status::invalidArgument("preset references unknown node: " + pipeline_id + "." + node_id);
      if (node->module_type.empty()) return Status::invalidArgument("preset node is not a module: " + pipeline_id + "." + node_id);

      const auto& values = requiredObject(entry_json, "values");
      const auto* handler = module_configs.find(node->module_type);
      if (handler == nullptr) return Status::unavailable("no module config handler registered for module_type: " + node->module_type);

      auto request = makeModuleConfigRequest(ir, *pipeline, *node, nullptr, values.dump(), preset.preset_id, entry_json.dump());
      auto status = handler->validatePreset(request);
      if (!status.ok()) return fromModuleConfigStatus(status);

      module_config::ModuleConfigBlob blob;
      status = handler->packPreset(request, blob);
      if (!status.ok()) return fromModuleConfigStatus(status);

      PresetEntryInfo entry;
      entry.preset_id = preset.preset_id;
      entry.pipeline_id = pipeline_id;
      entry.node_id = node_id;
      entry.inst_id = node->inst_id;
      entry.module_type = node->module_type;
      entry.source_json = entry_json.dump();
      entry.config_format = blob.format;
      entry.payload = std::move(blob.data);
      preset.entries.push_back(std::move(entry));
    }
    ir.presets.push_back(std::move(preset));
  }
  return Status::success();
}

std::vector<uint8_t> buildPrivatePayload(const ProjectIr& ir) {
  std::ostringstream json;
  json << "{";
  json << "\"format\":\"audio-studio-private-v1\",";
  json << "\"project\":" << quote(ir.project_name) << ",";
  json << "\"pipelines\":[";
  for (size_t i = 0; i < ir.pipelines.size(); ++i) {
    const auto& p = ir.pipelines[i];
    if (i != 0) json << ",";
    json << "{";
    json << "\"pipe_id\":" << quote(p.pipe_id) << ",";
    json << "\"name\":" << quote(p.name) << ",";
    json << "\"domain\":" << quote(p.domain) << ",";
    json << "\"direction\":" << quote(p.direction) << ",";
    json << "\"sample_rate\":" << p.sample_rate << ",";
    json << "\"channels_max\":" << p.channels_max << ",";
    json << "\"ports\":[";
    for (size_t j = 0; j < p.ports.size(); ++j) {
      const auto& port = p.ports[j];
      if (j != 0) json << ",";
      json << "{";
      json << "\"port_id\":" << quote(port.port_id) << ",";
      json << "\"role\":" << quote(port.role) << ",";
      json << "\"pcm_id\":" << quote(port.pcm_id) << ",";
      json << "\"dai_id\":" << quote(port.dai_id) << ",";
      json << "\"aif_role\":" << quote(port.aif_role) << ",";
      json << "\"transport\":" << quote(port.transport) << ",";
      json << "\"hw_id\":" << quote(port.hw_id) << ",";
      json << "\"hw_dir\":" << quote(port.hw_dir) << ",";
      json << "\"max_ch\":" << port.max_ch << ",";
      json << "\"tdm_slots\":" << port.tdm_slots << ",";
      json << "\"slot_width\":" << port.slot_width << ",";
      json << "\"sample_bits\":" << port.sample_bits << ",";
      json << "\"fsync_hz\":" << port.fsync_hz;
      json << "}";
    }
    json << "]}";
  }
  json << "],\"controls\":[";
  for (size_t i = 0; i < ir.controls.size(); ++i) {
    const auto& c = ir.controls[i];
    if (i != 0) json << ",";
    json << "{";
    json << "\"control_uid\":" << c.control_uid << ",";
    json << "\"type_uid\":" << c.type_uid << ",";
    json << "\"param_uid\":" << c.param_uid << ",";
    json << "\"pipe_id\":" << quote(c.pipe_id) << ",";
    json << "\"node_id\":" << quote(c.node_id) << ",";
    json << "\"inst_id\":" << quote(c.inst_id) << ",";
    json << "\"module_type\":" << quote(c.module_type) << ",";
    json << "\"param_id\":" << quote(c.param_id) << ",";
    json << "\"value_type\":" << quote(c.value_type) << ",";
    json << "\"config_format\":" << quote(c.config_format) << ",";
    json << "\"default_payload_hex\":" << quote(compactHex(c.default_payload));
    json << "}";
  }
  json << "],\"install_params\":[";
  for (size_t i = 0; i < ir.install_params.size(); ++i) {
    const auto& p = ir.install_params[i];
    if (i != 0) json << ",";
    json << "{";
    json << "\"param_uid\":" << p.param_uid << ",";
    json << "\"pipe_id\":" << quote(p.pipe_id) << ",";
    json << "\"node_id\":" << quote(p.node_id) << ",";
    json << "\"inst_id\":" << quote(p.inst_id) << ",";
    json << "\"module_type\":" << quote(p.module_type) << ",";
    json << "\"param_id\":" << quote(p.param_id) << ",";
    json << "\"config_format\":" << quote(p.config_format) << ",";
    json << "\"payload_hex\":" << quote(compactHex(p.payload));
    json << "}";
  }
  json << "],\"presets\":[";
  for (size_t i = 0; i < ir.presets.size(); ++i) {
    const auto& p = ir.presets[i];
    if (i != 0) json << ",";
    json << "{";
    json << "\"preset_uid\":" << p.preset_uid << ",";
    json << "\"preset_id\":" << quote(p.preset_id) << ",";
    json << "\"load_mode\":" << quote(p.load_mode) << ",";
    json << "\"validate_all_before_apply\":" << (p.validate_all_before_apply ? "true" : "false") << ",";
    json << "\"rollback_on_error\":" << (p.rollback_on_error ? "true" : "false") << ",";
    json << "\"apply_order\":" << quote(p.apply_order) << ",";
    json << "\"entries\":[";
    for (size_t j = 0; j < p.entries.size(); ++j) {
      const auto& e = p.entries[j];
      if (j != 0) json << ",";
      json << "{";
      json << "\"pipeline_id\":" << quote(e.pipeline_id) << ",";
      json << "\"node_id\":" << quote(e.node_id) << ",";
      json << "\"inst_id\":" << quote(e.inst_id) << ",";
      json << "\"module_type\":" << quote(e.module_type) << ",";
      json << "\"config_format\":" << quote(e.config_format) << ",";
      json << "\"payload_hex\":" << quote(compactHex(e.payload));
      json << "}";
    }
    json << "]}";
  }
  json << "]}";

  const std::string payload_json = json.str();
  const uint32_t payload_crc = crc32(reinterpret_cast<const uint8_t*>(payload_json.data()), payload_json.size());

  std::vector<uint8_t> out;
  appendLe32(out, kAsPrivateMagic);
  appendLe16(out, kAsPrivateVersionMajor);
  appendLe16(out, kAsPrivateVersionMinor);
  appendLe32(out, 24);
  appendLe32(out, static_cast<uint32_t>(payload_json.size()));
  appendLe32(out, payload_crc);
  appendLe32(out, 0);
  out.insert(out.end(), payload_json.begin(), payload_json.end());
  return out;
}

std::string controlConf(const ControlInfo& control) {
  std::ostringstream out;
  const std::string name = topologyQuote(control.control_name);
  if (control.value_type == "bytes") {
    out << "# " << control.control_name << " is described in AS_PRIVATE; no SectionControlBytes is emitted.\n\n";
    return out.str();
  }
  if (control.value_type == "enum") {
    out << "SectionText." << name << " { values [";
    for (const auto& value : control.enum_values) out << " " << topologyQuote(value);
    out << " ] }\n";
    out << "SectionControlEnum." << name << " {\n";
    out << "  index \"1\"\n";
    out << "  texts " << name << "\n";
    out << "  channel.\"FL\" { reg \"0\" shift \"0\" }\n";
    out << "  ops.\"ctl\" { info \"enum\" get \"257\" put \"257\" }\n";
    out << "}\n\n";
    return out.str();
  }

  const bool is_bool = control.value_type == "bool";
  int64_t max_value = is_bool ? 1 : 100;
  if (control.range.present) {
    const auto span = control.range.max - control.range.min;
    max_value = std::max<int64_t>(1, span / std::max<int64_t>(1, control.range.step));
  } else if (control.value_type == "uint8") {
    max_value = 255;
  } else if (control.value_type == "uint16") {
    max_value = 65535;
  }

  if (control.range.present && !is_bool) {
    const std::string tlv_name = topologyQuote(control.control_name + " TLV");
    out << "SectionTLV." << tlv_name << " {\n";
    out << "  scale { min " << topologyQuote(std::to_string(control.range.min * 100))
        << " step " << topologyQuote(std::to_string(control.range.step * 100)) << " mute \"0\" }\n";
    out << "}\n";
  }

  out << "SectionControlMixer." << name << " {\n";
  out << "  index \"1\"\n";
  out << "  channel.\"FL\" { reg \"0\" shift \"0\" }\n";
  out << "  channel.\"FR\" { reg \"0\" shift \"8\" }\n";
  out << "  max " << topologyQuote(std::to_string(max_value)) << "\n";
  out << "  invert \"false\"\n";
  out << "  ops.\"ctl\" { info \"volsw\" get " << topologyQuote(is_bool ? "259" : "256")
      << " put " << topologyQuote(is_bool ? "259" : "256") << " }\n";
  if (control.range.present && !is_bool) out << "  tlv " << topologyQuote(control.control_name + " TLV") << "\n";
  out << "}\n\n";
  return out.str();
}

std::string widgetType(const NodeInfo& node, const PipelineInfo& pipeline) {
  if (node.kind == "port") return pipeline.direction == "capture" ? "aif_out" : "aif_in";
  if (node.module_type == "gain.volume") return "pga";
  return "mixer";
}

std::string generateAlsaConf(const ProjectIr& ir, const std::vector<uint8_t>& private_data) {
  std::ostringstream out;
  out << "# Generated by Audio Studio as_config. Do not edit by hand.\n\n";
  out << "SectionData.\"AS_PRIVATE\" {\n";
  out << "  bytes " << topologyQuote(hexBytes(private_data)) << "\n";
  out << "}\n";
  out << "SectionManifest.\"sof_manifest\" {\n";
  out << "  data [ \"AS_PRIVATE\" ]\n";
  out << "}\n\n";

  for (const auto& control : ir.controls) out << controlConf(control);

  for (const auto& pipeline : ir.pipelines) {
    for (const auto& node : pipeline.nodes) {
      out << "SectionWidget." << topologyQuote(node.widget_name) << " {\n";
      out << "  index \"1\"\n";
      out << "  type " << topologyQuote(widgetType(node, pipeline)) << "\n";
      out << "  no_pm \"true\"\n";
      out << "  shift \"0\"\n";
      out << "  invert \"0\"\n";
      out << "}\n\n";
    }
  }

  for (const auto& pipeline : ir.pipelines) {
    const std::string pcm_name = "A2 " + pipeline.name;
    const std::string caps_name = pcm_name + " Capabilities";
    out << "SectionPCMCapabilities." << topologyQuote(caps_name) << " {\n";
    out << "  formats \"S16_LE,S24_LE,S32_LE\"\n";
    out << "  rate_min \"8000\"\n";
    out << "  rate_max " << topologyQuote(std::to_string(std::max<uint32_t>(pipeline.sample_rate, 192000))) << "\n";
    out << "  channels_min " << topologyQuote(std::to_string(pipeline.channels_min)) << "\n";
    out << "  channels_max " << topologyQuote(std::to_string(pipeline.channels_max)) << "\n";
    out << "}\n\n";
    out << "SectionPCM." << topologyQuote(pcm_name) << " {\n";
    out << "  index \"1\"\n";
    out << "  id " << topologyQuote(std::to_string(pipeline.pcm_index)) << "\n";
    out << "  dai." << topologyQuote(pcm_name + " Pin") << " { id "
        << topologyQuote(std::to_string(pipeline.pcm_index)) << " }\n";
    out << "  pcm." << topologyQuote(pipeline.direction) << " { capabilities " << topologyQuote(caps_name) << " }\n";
    out << "}\n\n";
  }

  out << "SectionGraph.\"a2\" {\n";
  out << "  index \"1\"\n";
  out << "  lines [\n";
  for (const auto& pipeline : ir.pipelines) {
    for (const auto& edge : pipeline.edges) {
      const auto* from = findNode(pipeline, edge.from_node);
      const auto* to = findNode(pipeline, edge.to_node);
      if (from == nullptr || to == nullptr) continue;
      out << "    " << topologyQuote(to->widget_name + ", , " + from->widget_name) << "\n";
    }
  }
  out << "  ]\n";
  out << "}\n";
  return out.str();
}

std::string generateIdsHeader(const ProjectIr& ir) {
  std::ostringstream out;
  out << "#pragma once\n\n";
  out << "/* Generated by Audio Studio as_config. */\n\n";
  for (const auto& item : ir.module_types) {
    const auto& type = item.second;
    out << "#define AS_MODULE_TYPE_" << macroToken(type.type_id) << " " << hex32(stableUid("type", type.type_id)) << "u\n";
    for (const auto& param : type.parameters) {
      out << "#define AS_PARAM_" << macroToken(type.type_id + "_" + param.param_id) << " "
          << hex32(stableUid("param", type.type_id + ":" + param.param_id)) << "u\n";
    }
  }
  out << "\n";
  for (const auto& control : ir.controls) {
    out << "#define " << control.macro_name << " " << hex32(control.control_uid) << "u\n";
  }
  return out.str();
}

std::string generatePrivateHeader() {
  std::ostringstream out;
  out << "#pragma once\n\n";
  out << "#include <stdint.h>\n\n";
  out << "#define AS_TPLG_PRIVATE_MAGIC 0x46435341u\n";
  out << "#define AS_TPLG_PRIVATE_VERSION_MAJOR 1u\n";
  out << "#define AS_TPLG_PRIVATE_VERSION_MINOR 0u\n\n";
  out << "struct as_tplg_private_header {\n";
  out << "    uint32_t magic;\n";
  out << "    uint16_t version_major;\n";
  out << "    uint16_t version_minor;\n";
  out << "    uint32_t header_size;\n";
  out << "    uint32_t payload_size;\n";
  out << "    uint32_t payload_crc32;\n";
  out << "    uint32_t flags;\n";
  out << "};\n";
  return out.str();
}

std::string generatePresetHeader(const ProjectIr& ir) {
  std::ostringstream out;
  out << "#pragma once\n\n";
  out << "/* Generated by Audio Studio as_config. */\n\n";
  for (const auto& preset : ir.presets) {
    out << "#define AS_PRESET_" << macroToken(preset.preset_id) << " " << hex32(preset.preset_uid) << "u\n";
  }
  return out.str();
}

std::string generateControlsCsv(const ProjectIr& ir) {
  std::ostringstream out;
  out << "control_uid,pipe_id,node_id,inst_id,module_type,param_id,param_name,value_type,alsa_control_name\n";
  for (const auto& c : ir.controls) {
    out << hex32(c.control_uid) << ","
        << csvEscape(c.pipe_id) << ","
        << csvEscape(c.node_id) << ","
        << csvEscape(c.inst_id) << ","
        << csvEscape(c.module_type) << ","
        << csvEscape(c.param_id) << ","
        << csvEscape(c.param_name) << ","
        << csvEscape(c.value_type) << ","
        << csvEscape(c.control_name) << "\n";
  }
  return out.str();
}

std::string generateReport(const ConfigCompileRequest& request,
                           const ConfigCompileOutput& output,
                           const ProjectIr& ir,
                           const std::vector<std::string>& handler_ids) {
  (void)ir;
  std::ostringstream out;
  out << "{";
  out << "\"ok\":" << (output.ok ? "true" : "false") << ",";
  out << "\"build_tplg\":" << (request.build_tplg ? "true" : "false") << ",";
  out << "\"tplg_built\":" << (output.tplg_built ? "true" : "false") << ",";
  out << "\"input_path\":" << quote(request.input_path) << ",";
  out << "\"output_dir\":" << quote(request.output_dir) << ",";
  out << "\"conf_path\":" << quote(output.conf_path) << ",";
  out << "\"tplg_path\":" << quote(output.tplg_path) << ",";
  out << "\"private_bin_path\":" << quote(output.private_bin_path) << ",";
  out << "\"ids_header_path\":" << quote(output.ids_header_path) << ",";
  out << "\"private_header_path\":" << quote(output.private_header_path) << ",";
  out << "\"preset_header_path\":" << quote(output.preset_header_path) << ",";
  out << "\"controls_csv_path\":" << quote(output.controls_csv_path) << ",";
  out << "\"alsatplg_log_path\":" << quote(output.alsatplg_log_path) << ",";
  out << "\"tplg_decode_conf_path\":" << quote(output.tplg_decode_conf_path) << ",";
  out << "\"tplg_decode_log_path\":" << quote(output.tplg_decode_log_path) << ",";
  out << "\"module_type_count\":" << output.module_type_count << ",";
  out << "\"module_instance_count\":" << output.module_instance_count << ",";
  out << "\"pipeline_count\":" << output.pipeline_count << ",";
  out << "\"runtime_control_count\":" << output.runtime_control_count << ",";
  out << "\"install_param_count\":" << output.install_param_count << ",";
  out << "\"preset_count\":" << output.preset_count << ",";
  out << "\"plugin_count\":" << output.plugin_count << ",";
  out << "\"warnings\":[";
  for (size_t i = 0; i < output.warnings.size(); ++i) {
    if (i != 0) out << ",";
    out << quote(output.warnings[i]);
  }
  out << "],\"module_config_handlers\":[";
  for (size_t i = 0; i < handler_ids.size(); ++i) {
    if (i != 0) out << ",";
    out << quote(handler_ids[i]);
  }
  out << "]}";
  return out.str();
}

JsonValue outputToJson(const ConfigCompileOutput& output) {
  JsonValue result = JsonValue::object();
  result["ok"] = output.ok;
  result["tplg_built"] = output.tplg_built;
  result["conf_path"] = output.conf_path;
  result["tplg_path"] = output.tplg_path;
  result["private_bin_path"] = output.private_bin_path;
  result["ids_header_path"] = output.ids_header_path;
  result["private_header_path"] = output.private_header_path;
  result["preset_header_path"] = output.preset_header_path;
  result["controls_csv_path"] = output.controls_csv_path;
  result["report_path"] = output.report_path;
  result["alsatplg_log_path"] = output.alsatplg_log_path;
  result["tplg_decode_conf_path"] = output.tplg_decode_conf_path;
  result["tplg_decode_log_path"] = output.tplg_decode_log_path;
  result["module_type_count"] = static_cast<uint32_t>(output.module_type_count);
  result["module_instance_count"] = static_cast<uint32_t>(output.module_instance_count);
  result["pipeline_count"] = static_cast<uint32_t>(output.pipeline_count);
  result["runtime_control_count"] = static_cast<uint32_t>(output.runtime_control_count);
  result["install_param_count"] = static_cast<uint32_t>(output.install_param_count);
  result["preset_count"] = static_cast<uint32_t>(output.preset_count);
  result["plugin_count"] = static_cast<uint32_t>(output.plugin_count);
  JsonValue warnings = JsonValue::array();
  for (const auto& warning : output.warnings) warnings.pushBack(warning);
  result["warnings"] = std::move(warnings);
  return result;
}

Status loadPlugins(drivers::dynlib::IDynlibDriver* dynlib,
                   const std::vector<std::string>& paths,
                   ModuleConfigRegistry& registry,
                   std::vector<std::unique_ptr<drivers::dynlib::IDynlib>>& libraries) {
  if (paths.empty()) return Status::success();
  if (dynlib == nullptr) return Status::unavailable("dynamic library driver is not configured");
  for (const auto& path : paths) {
    if (!dynlib->isValidLibraryFile(path)) return Status::invalidArgument("invalid plugin library file: " + path);
    auto library = dynlib->createLibrary();
    if (!library) return Status::internal("failed to create dynamic library handle");
    auto status = library->open(path, {});
    if (!status.ok()) return status;
    void* symbol = nullptr;
    status = library->getSymbol(module_config::kRegisterModuleConfigHandlersSymbol, &symbol);
    if (!status.ok()) return status;
    auto register_fn = reinterpret_cast<module_config::RegisterModuleConfigHandlersFn>(symbol);
    if (!register_fn(registry)) return Status::internal("module config plugin registration failed: " + path);
    libraries.push_back(std::move(library));
  }
  return Status::success();
}

} // namespace

module_config::Status ModuleConfigRegistry::registerHandler(std::unique_ptr<module_config::IModuleConfigHandler> handler) {
  if (!handler) return module_config::Status::invalidArgument("module config handler is null");
  if (handler->id().empty()) return module_config::Status::invalidArgument("module config handler id is empty");
  if (handler->moduleType().empty()) return module_config::Status::invalidArgument("module config handler module type is empty");
  for (const auto& existing : handlers_) {
    if (existing->moduleType() == handler->moduleType()) {
      return module_config::Status::invalidArgument("module config handler already registered for module_type: " + handler->moduleType());
    }
  }
  handlers_.push_back(std::move(handler));
  return module_config::Status::success();
}

const module_config::IModuleConfigHandler* ModuleConfigRegistry::find(const std::string& module_type) const {
  const module_config::IModuleConfigHandler* fallback = nullptr;
  for (const auto& handler : handlers_) {
    if (handler->moduleType() == module_type) return handler.get();
    if (handler->moduleType() == "*") fallback = handler.get();
  }
  return fallback;
}

const module_config::IModuleConfigHandler* ModuleConfigRegistry::findExact(const std::string& module_type) const {
  for (const auto& handler : handlers_) {
    if (handler->moduleType() == module_type) return handler.get();
  }
  return nullptr;
}

std::vector<std::string> ModuleConfigRegistry::handlerIds() const {
  std::vector<std::string> ids;
  ids.reserve(handlers_.size());
  for (const auto& handler : handlers_) ids.push_back(handler->id());
  return ids;
}

size_t ModuleConfigRegistry::size() const {
  return handlers_.size();
}

ConfigService::ConfigService() {
  (void)module_configs_.registerHandler(std::make_unique<GenericModuleConfigHandler>());
}

ConfigService::ConfigService(drivers::filesystem::IFileSystemDriver* filesystem,
                             drivers::os::IOsDriver* os,
                             drivers::dynlib::IDynlibDriver* dynlib)
  : ConfigService() {
  setDrivers(filesystem, os, dynlib);
}

void ConfigService::setDrivers(drivers::filesystem::IFileSystemDriver* filesystem,
                               drivers::os::IOsDriver* os,
                               drivers::dynlib::IDynlibDriver* dynlib) {
  filesystem_ = filesystem;
  os_ = os;
  dynlib_ = dynlib;
}

ModuleConfigRegistry& ConfigService::moduleConfigs() {
  return module_configs_;
}

const ModuleConfigRegistry& ConfigService::moduleConfigs() const {
  return module_configs_;
}

Status ConfigService::compile(const ConfigCompileRequest& request, ConfigCompileOutput& output) {
  output = {};
  if (filesystem_ == nullptr) return Status::unavailable("filesystem driver is not configured");
  if (request.input_path.empty()) return Status::invalidArgument("config input path is empty");
  if (request.output_dir.empty()) return Status::invalidArgument("config output directory is empty");
  if (request.build_tplg && !kHostSupportsAlsaTplg) {
    return Status::unavailable("alsatplg compile/decode validation is supported on Linux hosts only; set build_tplg=false");
  }

  const size_t plugins_before = plugin_libraries_.size();
  auto status = loadPlugins(dynlib_, request.plugin_paths, module_configs_, plugin_libraries_);
  if (!status.ok()) return status;

  status = filesystem_->createDirectory(request.output_dir, true);
  if (!status.ok()) return status;
  const std::string include_dir = pathJoin(*filesystem_, request.output_dir, "include");
  status = filesystem_->createDirectory(include_dir, true);
  if (!status.ok()) return status;

  std::string json_text;
  status = readText(*filesystem_, request.input_path, json_text);
  if (!status.ok()) return status;

  ProjectIr ir;
  try {
    const auto root = audio_studio::rpc::parseJson(json_text);
    ir = parseProject(root, request.project_name);
    status = parseModuleTypesWithHandlers(ir, module_configs_);
    if (!status.ok()) return status;
    status = buildParameterTables(ir, module_configs_);
    if (!status.ok()) return status;
    status = buildPresets(root, ir, module_configs_);
    if (!status.ok()) return status;
  } catch (const std::exception& error) {
    return Status::invalidArgument(error.what());
  }

  output.module_type_count = ir.module_types.size();
  output.module_instance_count = ir.instances.size();
  output.pipeline_count = ir.pipelines.size();
  output.runtime_control_count = ir.controls.size();
  output.install_param_count = ir.install_params.size();
  output.preset_count = ir.presets.size();
  output.plugin_count = plugin_libraries_.size() - plugins_before;

  const auto private_data = buildPrivatePayload(ir);
  output.conf_path = pathJoin(*filesystem_, request.output_dir, request.project_name + ".conf");
  output.tplg_path = pathJoin(*filesystem_, request.output_dir, request.project_name + ".tplg");
  output.private_bin_path = pathJoin(*filesystem_, request.output_dir, request.project_name + "_private.bin");
  output.ids_header_path = pathJoin(*filesystem_, include_dir, "as_config_ids.h");
  output.private_header_path = pathJoin(*filesystem_, include_dir, "as_tplg_private.h");
  output.preset_header_path = pathJoin(*filesystem_, include_dir, "as_preset_ids.h");
  output.controls_csv_path = pathJoin(*filesystem_, request.output_dir, request.project_name + "_controls.csv");
  output.report_path = pathJoin(*filesystem_, request.output_dir, request.project_name + "_compile_report.json");
  output.alsatplg_log_path = pathJoin(*filesystem_, request.output_dir, request.project_name + "_alsatplg.log");
  output.tplg_decode_conf_path = pathJoin(*filesystem_, request.output_dir, request.project_name + "_decode.conf");
  output.tplg_decode_log_path = pathJoin(*filesystem_, request.output_dir, request.project_name + "_decode.log");

  status = writeText(*filesystem_, output.conf_path, generateAlsaConf(ir, private_data));
  if (!status.ok()) return status;
  status = writeBytes(*filesystem_, output.private_bin_path, private_data);
  if (!status.ok()) return status;
  status = writeText(*filesystem_, output.ids_header_path, generateIdsHeader(ir));
  if (!status.ok()) return status;
  status = writeText(*filesystem_, output.private_header_path, generatePrivateHeader());
  if (!status.ok()) return status;
  status = writeText(*filesystem_, output.preset_header_path, generatePresetHeader(ir));
  if (!status.ok()) return status;
  status = writeText(*filesystem_, output.controls_csv_path, generateControlsCsv(ir));
  if (!status.ok()) return status;

  if (request.build_tplg) {
    if (os_ == nullptr) return Status::unavailable("OS driver is not configured");
    const std::string command = shellQuote(request.alsatplg) + " -c " + shellQuote(output.conf_path) +
                                " -o " + shellQuote(output.tplg_path) + " > " + shellQuote(output.alsatplg_log_path) + " 2>&1";
    int exit_code = -1;
    status = os_->process().runCommand(command, exit_code);
    if (!status.ok()) return status;
    if (exit_code != 0) return Status::internal("alsatplg failed with exit code " + std::to_string(exit_code));

    std::string alsatplg_log;
    status = readText(*filesystem_, output.alsatplg_log_path, alsatplg_log);
    if (!status.ok()) return status;
    if (containsAlsaTopologyError(alsatplg_log)) {
      return Status::internal("alsatplg reported topology errors; see " + output.alsatplg_log_path);
    }

    const std::string decode_command = shellQuote(request.alsatplg) + " -d " + shellQuote(output.tplg_path) +
                                       " -o " + shellQuote(output.tplg_decode_conf_path) +
                                       " > " + shellQuote(output.tplg_decode_log_path) + " 2>&1";
    exit_code = -1;
    status = os_->process().runCommand(decode_command, exit_code);
    if (!status.ok()) return status;
    if (exit_code != 0) return Status::internal("alsatplg decode failed with exit code " + std::to_string(exit_code));

    std::string decode_log;
    status = readText(*filesystem_, output.tplg_decode_log_path, decode_log);
    if (!status.ok()) return status;
    if (containsAlsaTopologyError(decode_log)) {
      return Status::internal("alsatplg decode reported topology errors; see " + output.tplg_decode_log_path);
    }
    output.tplg_built = true;
  }

  output.ok = true;
  output.warnings = ir.warnings;
  status = writeText(*filesystem_, output.report_path, generateReport(request, output, ir, module_configs_.handlerIds()));
  if (!status.ok()) return status;

  (void)outputToJson(output);
  return Status::success();
}

} // namespace audio_studio::framework::config
