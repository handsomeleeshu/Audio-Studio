#include "config_service.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

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
  double min = 0.0;
  double max = 0.0;
  double step = 1.0;
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
  std::string module_uuid;
  std::string source_json;
  std::vector<ParameterInfo> parameters;
};

struct PortInfo {
  std::string port_id;
  std::string role;
  std::string resource_ref;
  std::string pcm_id;
  std::string dai_id;
  std::string aif_role;
  std::string transport;
  std::string hw_id;
  std::string hw_dir;
  bool has_dai_index = false;
  uint32_t dai_index = 0;
  uint32_t min_ch = 0;
  uint32_t max_ch = 0;
  uint32_t tdm_slots = 0;
  uint32_t slot_width = 0;
  uint32_t sample_bits = 0;
  uint32_t fsync_hz = 0;
};

struct NodeInfo {
  std::string node_id;
  std::string inst_id;
  std::string name;
  std::string module_type;
  std::string widget_name;
  std::string source_json;
  std::map<std::string, std::string> params_json;
  bool endpoint_present = false;
  PortInfo endpoint;
};

struct EdgeInfo {
  std::string from_node;
  std::string to_node;
};

struct CoreResourceInfo {
  std::string resource_id;
  uint32_t core = 0;
};

struct ResourceCatalogInfo {
  std::map<std::string, CoreResourceInfo> cores;
};

struct SofSchedulingInfo {
  uint32_t period_us = 10000;
  uint32_t priority = 0;
  uint32_t core = 0;
  uint32_t time_domain = 0;
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
  uint32_t sample_bits = 32;
  SofSchedulingInfo scheduling;
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
  bool emit_topology = true;
  std::string topology_control_name;
  std::vector<uint8_t> topology_default_payload;
};

struct WidgetControlRefs {
  std::vector<std::string> mixer;
  std::vector<std::string> enumerated;
  std::vector<std::string> bytes;
};

struct InferredBufferInfo {
  size_t edge_index = 0;
  std::string widget_name;
  uint32_t size_bytes = 0;
  uint32_t caps = 0;
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
    if (isBytesParameter(request.parameter_json)) {
      try {
        const auto values = audio_studio::rpc::parseJson(request.values_json);
        if (!values.isObject() || !values.has(request.parameter_id)) {
          return module_config::Status::invalidArgument("bytes parameter value missing: " + request.parameter_id);
        }
        out.format = "sof-ipc3-bytes-v1";
        out.data = parseRawBytes(values.at(request.parameter_id));
        return module_config::Status::success();
      } catch (const std::exception& error) {
        return module_config::Status::invalidArgument(
            "invalid bytes parameter payload for " + request.parameter_id + ": " + error.what());
      }
    }
    return packJson(request, out, "as-generic-runtime-json-v1");
  }

private:
  static bool isBytesParameter(const std::string& parameter_json) {
    if (parameter_json.empty()) return false;
    const auto parameter = audio_studio::rpc::parseJson(parameter_json);
    return parameter.isObject() && parameter.has("value_type") &&
           parameter.at("value_type").isString() &&
           parameter.at("value_type").asString() == "bytes";
  }

  static std::vector<uint8_t> parseRawByteArray(const JsonValue& value) {
    std::vector<uint8_t> bytes;
    bytes.reserve(value.asArray().size());
    for (const auto& item : value.asArray()) {
      if (!item.isNumber()) throw std::runtime_error("byte array items must be numbers");
      const auto byte = item.asUInt64();
      if (byte > 0xffu) throw std::runtime_error("byte value is larger than 255");
      bytes.push_back(static_cast<uint8_t>(byte));
    }
    return bytes;
  }

  static std::vector<uint8_t> parseRawByteString(std::string text) {
    for (auto& ch : text) {
      if (ch == ',' || ch == ':' || std::isspace(static_cast<unsigned char>(ch))) ch = ' ';
    }

    std::istringstream input(text);
    std::string token;
    std::vector<uint8_t> bytes;
    while (input >> token) {
      size_t parsed = 0;
      const unsigned long value = std::stoul(token, &parsed, 0);
      if (parsed != token.size()) throw std::runtime_error("invalid byte token: " + token);
      if (value > 0xfful) throw std::runtime_error("byte value is larger than 255: " + token);
      bytes.push_back(static_cast<uint8_t>(value));
    }
    return bytes;
  }

  static std::vector<uint8_t> parseRawBytes(const JsonValue& value) {
    if (value.isArray()) return parseRawByteArray(value);
    if (value.isString()) return parseRawByteString(value.asString());
    if (value.isObject() && value.has("bytes")) return parseRawBytes(value.at("bytes"));
    throw std::runtime_error("bytes value must be an array, string, or object with a bytes field");
  }

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

uint32_t uintValueAny(const JsonValue& object,
                      const std::vector<std::string>& keys,
                      uint32_t fallback) {
  for (const auto& key : keys) {
    if (object.isObject() && object.has(key) && !object.at(key).isNull()) return uintValue(object, key, fallback);
  }
  return fallback;
}

bool boolValue(const JsonValue& object, const std::string& key, bool fallback) {
  if (!object.isObject() || !object.has(key) || object.at(key).isNull()) return fallback;
  if (!object.at(key).isBool()) throw std::runtime_error("JSON field must be bool: " + key);
  return object.at(key).asBool();
}

const JsonValue* optionalObject(const JsonValue& object, const std::string& key) {
  if (!object.isObject() || !object.has(key) || object.at(key).isNull()) return nullptr;
  if (!object.at(key).isObject()) throw std::runtime_error("JSON field must be object: " + key);
  return &object.at(key);
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

std::map<std::string, std::string> jsonObjectValueMap(const JsonValue& object, const std::string& key) {
  std::map<std::string, std::string> out;
  if (!object.isObject() || !object.has(key) || object.at(key).isNull()) return out;
  const auto& values = requiredObject(object, key);
  for (const auto& item : values.asObject()) out[item.first] = item.second.dump();
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
  range.min = object.has("min") ? object.at("min").asDouble() : 0.0;
  range.max = object.has("max") ? object.at("max").asDouble() : 0.0;
  range.step = object.has("step") ? object.at("step").asDouble() : 1.0;
  if (range.step <= 0.0) range.step = 1.0;
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

bool regularFileExists(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec) && !ec;
}

std::string executableDirectory(drivers::os::IOsDriver* os) {
  if (os == nullptr) return {};
  const auto executable = os->process().executablePath();
  if (executable.empty()) return {};
  return std::filesystem::path(executable).parent_path().string();
}

std::string findFileFromRoots(const std::vector<std::filesystem::path>& roots,
                              const std::filesystem::path& relative_path) {
  for (auto root : roots) {
    if (root.empty()) continue;
    std::error_code ec;
    root = std::filesystem::absolute(root, ec);
    if (ec) continue;
    while (true) {
      const auto candidate = root / relative_path;
      if (regularFileExists(candidate)) return candidate.string();

      const auto parent = root.parent_path();
      if (parent == root || parent.empty()) break;
      root = parent;
    }
  }
  return {};
}

std::string discoverAlsaTplgExecutable(drivers::os::IOsDriver* os) {
  if (const char* path = std::getenv("AUDIO_STUDIO_ALSATPLG"); path != nullptr && path[0] != '\0') {
    return path;
  }

  std::error_code ec;
  auto current = std::filesystem::current_path(ec);
  std::vector<std::filesystem::path> roots;
  if (!ec) roots.push_back(current);
  const auto exe_dir = executableDirectory(os);
  if (!exe_dir.empty()) roots.emplace_back(exe_dir);

  const auto bundled = findFileFromRoots(roots, std::filesystem::path("third_party") / "alsatplg" / "bin" / "alsatplg");
  if (!bundled.empty()) {
    return bundled;
  }

  return "alsatplg";
}

std::string resolveAlsaTplgExecutable(const std::string& requested, drivers::os::IOsDriver* os) {
  if (requested.empty() || requested == "alsatplg") return discoverAlsaTplgExecutable(os);
  return requested;
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

std::string colonHexBytes(const std::vector<uint8_t>& bytes) {
  std::ostringstream out;
  for (size_t i = 0; i < bytes.size(); ++i) {
    if (i != 0) out << ":";
    out << std::hex << std::nouppercase << std::setw(2) << std::setfill('0')
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

uint8_t hexNibble(const char ch, const std::string& context) {
  if (ch >= '0' && ch <= '9') return static_cast<uint8_t>(ch - '0');
  if (ch >= 'a' && ch <= 'f') return static_cast<uint8_t>(10 + ch - 'a');
  if (ch >= 'A' && ch <= 'F') return static_cast<uint8_t>(10 + ch - 'A');
  throw std::runtime_error("Invalid hexadecimal digit in " + context);
}

std::vector<uint8_t> uuidBytes(const std::string& uuid) {
  std::string hex;
  hex.reserve(32);
  for (const char ch : uuid) {
    if (ch == '-') continue;
    hex.push_back(ch);
  }
  if (hex.size() != 32) {
    throw std::runtime_error("SOF UUID must contain 16 bytes: " + uuid);
  }

  std::vector<uint8_t> bytes;
  bytes.reserve(16);
  for (size_t i = 0; i < hex.size(); i += 2) {
    const uint8_t hi = hexNibble(hex[i], uuid);
    const uint8_t lo = hexNibble(hex[i + 1], uuid);
    bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return bytes;
}

std::vector<uint8_t> sofUuidTokenBytes(const std::string& uuid) {
  auto bytes = uuidBytes(uuid);
  std::reverse(bytes.begin(), bytes.begin() + 4);
  std::reverse(bytes.begin() + 4, bytes.begin() + 6);
  std::reverse(bytes.begin() + 6, bytes.begin() + 8);
  return bytes;
}

std::string trim(const std::string& value) {
  size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
  size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
  return value.substr(begin, end - begin);
}

std::string lowerAscii(std::string value) {
  for (auto& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return value;
}

std::string findSofUuidRegistryPath() {
  if (const char* path = std::getenv("SOF_UUID_REGISTRY"); path != nullptr && path[0] != '\0') {
    return path;
  }

  std::error_code ec;
  auto current = std::filesystem::current_path(ec);
  if (ec) return {};

  while (true) {
    const auto candidate = current / "sof" / "uuid-registry.txt";
    if (std::filesystem::exists(candidate, ec) && !ec) return candidate.string();
    if (current == current.root_path()) break;
    current = current.parent_path();
  }
  return {};
}

std::map<std::string, std::string> loadSofUuidRegistry() {
  const auto registry_path = findSofUuidRegistryPath();
  if (registry_path.empty()) {
    throw std::runtime_error("SOF UUID registry not found; set SOF_UUID_REGISTRY or run from the VASS workspace");
  }

  std::ifstream input(registry_path);
  if (!input) throw std::runtime_error("Unable to open SOF UUID registry: " + registry_path);

  std::map<std::string, std::string> registry;
  std::string line;
  size_t line_no = 0;
  while (std::getline(input, line)) {
    ++line_no;
    const auto comment = line.find('#');
    if (comment != std::string::npos) line.erase(comment);
    line = trim(line);
    if (line.empty()) continue;

    std::istringstream parts(line);
    std::string uuid;
    std::string name;
    std::string extra;
    if (!(parts >> uuid >> name) || (parts >> extra)) {
      throw std::runtime_error("Invalid SOF UUID registry line " + std::to_string(line_no) + ": " + registry_path);
    }

    (void)uuidBytes(uuid);
    registry.emplace(name, lowerAscii(uuid));
  }

  return registry;
}

const std::map<std::string, std::string>& builtinSofUuidRegistry() {
  static const std::map<std::string, std::string> registry = {
    {"asrc", "c8ec72f6-8526-4faf-9d39a23d0b541de2"},
    {"chremap", "74e4d7a4-c015-46b8-aa38a3b38cfab0d8"},
    {"crossover", "948c9ad1-806a-4131-ad6cb2bda9e35a9f"},
    {"dai", "c2b00d27-ffbc-4150-a51a245c79c5e54b"},
    {"dcblock", "b809efaf-5681-42b1-9ed604bb012dd384"},
    {"delay_line", "71ae339d-d0fe-4995-8705e7bc44efd974"},
    {"dsp_filter", "6a010917-29dd-4cd8-950e6bc077e5476a"},
    {"drc", "b36ee4da-006f-47f9-a06dfecbe2d8b6ce"},
    {"eq_fir", "43a90ce7-f3a5-41df-ac06ba98651ae6a3"},
    {"eq_iir", "5150c0e6-27f9-4ec8-8351c705b642d12f"},
    {"fader_balance", "5c057768-21f0-4782-8056bacecf20e9fc"},
    {"file_io_dai", "6bd5a21f-2445-7917-5183c36b8bf39368"},
    {"host", "8b9d100c-6d78-418f-90a3e0e805d0852b"},
    {"kpb", "d8218443-5ff3-4a4c-b3886cfe07b9562e"},
    {"mixer", "bc06c037-12aa-417c-9a9789282e321a76"},
    {"multiband_drc", "0d9f2256-8e4f-47b3-8448239a334f1191"},
    {"mux", "c607ff4d-9cb6-49dc-b6787da3c63ea557"},
    {"selector", "55a88ed5-3d18-46ca-88f10ee6eae9930f"},
    {"src", "c1c5326d-8390-46b4-aa4795c3beca6550"},
    {"up_down_mixer", "42f8060c-832f-4dbf-b24751e961997b34"},
    {"volume", "b77e677e-5ff4-4188-af14fba8bdbf8682"},
  };
  return registry;
}

std::string sofUuidFromRegistry(const std::string& registry_name) {
  if (!findSofUuidRegistryPath().empty()) {
    static const auto registry = loadSofUuidRegistry();
    const auto it = registry.find(registry_name);
    if (it == registry.end()) throw std::runtime_error("SOF UUID registry entry not found: " + registry_name);
    return it->second;
  }

  const auto& fallback = builtinSofUuidRegistry();
  const auto fallback_it = fallback.find(registry_name);
  if (fallback_it != fallback.end()) return fallback_it->second;

  throw std::runtime_error("SOF UUID registry not found; set SOF_UUID_REGISTRY or run from the VASS workspace");
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

uint32_t maxChannelsFromPortInfos(const std::vector<PortInfo>& ports) {
  uint32_t max_channels = 2;
  for (const auto& port : ports) {
    if (port.max_ch != 0) max_channels = std::max(max_channels, port.max_ch);
  }
  return max_channels;
}

uint32_t minChannelsFromPortInfos(const std::vector<PortInfo>& ports) {
  uint32_t min_channels = 0;
  for (const auto& port : ports) {
    if (port.min_ch == 0) continue;
    min_channels = min_channels == 0 ? port.min_ch : std::min(min_channels, port.min_ch);
  }
  return min_channels == 0 ? 1 : min_channels;
}

uint32_t sampleBitsFromPortInfos(const std::vector<PortInfo>& ports) {
  uint32_t sample_bits = 0;
  for (const auto& port : ports) {
    if (port.sample_bits != 0) sample_bits = std::max(sample_bits, port.sample_bits);
  }
  return sample_bits == 0 ? 32 : sample_bits;
}

uint32_t sampleRateFromPortInfos(const std::vector<PortInfo>& ports, uint32_t fallback) {
  for (const auto& port : ports) {
    if (port.role == "dai" && port.fsync_hz != 0) return port.fsync_hz;
  }
  for (const auto& port : ports) {
    if (port.fsync_hz != 0) return port.fsync_hz;
  }
  return fallback == 0 ? 48000 : fallback;
}

uint32_t firstNumericArrayItem(const JsonValue& object, const std::string& key, uint32_t fallback) {
  if (!object.isObject() || !object.has(key) || object.at(key).isNull()) return fallback;
  const auto& value = object.at(key);
  if (value.isNumber()) return static_cast<uint32_t>(value.asUInt64());
  if (!value.isArray() || value.asArray().empty()) throw std::runtime_error("JSON field must be number or non-empty array: " + key);
  if (!value.asArray().front().isNumber()) throw std::runtime_error("JSON array first item must be number: " + key);
  return static_cast<uint32_t>(value.asArray().front().asUInt64());
}

void applyCapabilitiesToPort(PortInfo& port, const JsonValue& object, uint32_t default_rate) {
  const JsonValue* caps = optionalObject(object, "capabilities");
  if (caps == nullptr) caps = &object;
  if (caps->has("channels") && caps->at("channels").isObject()) {
    port.min_ch = uintValue(caps->at("channels"), "min", port.min_ch);
    port.max_ch = uintValue(caps->at("channels"), "max", port.max_ch);
  } else if (caps->has("channels") && caps->at("channels").isNumber()) {
    port.min_ch = uintValue(*caps, "channels", port.min_ch);
    port.max_ch = uintValue(*caps, "channels", port.max_ch);
  }
  port.min_ch = uintValueAny(*caps, {"min_ch", "channels_min"}, port.min_ch);
  port.max_ch = uintValueAny(*caps, {"max_ch", "channels_max"}, port.max_ch);
  port.tdm_slots = uintValue(*caps, "tdm_slots", port.tdm_slots == 0 ? port.max_ch : port.tdm_slots);
  port.slot_width = uintValue(*caps, "slot_width", port.slot_width == 0 ? 32 : port.slot_width);
  port.sample_bits = firstNumericArrayItem(*caps, "sample_bits", port.sample_bits == 0 ? 32 : port.sample_bits);
  port.fsync_hz = firstNumericArrayItem(*caps, "sample_rates", port.fsync_hz == 0 ? default_rate : port.fsync_hz);
  port.fsync_hz = uintValueAny(*caps, {"fsync_hz", "sample_rate"}, port.fsync_hz == 0 ? default_rate : port.fsync_hz);
}

bool isHostModuleType(const std::string& module_type) {
  return module_type == "builtin.host" || module_type == "host";
}

bool isDaiModuleType(const std::string& module_type) {
  return module_type == "builtin.dai" || module_type == "dai";
}

PortInfo parseEndpointNode(const JsonValue& node_json, const JsonValue& params, uint32_t default_rate) {
  PortInfo port;
  port.port_id = stringValue(node_json, "node_id");
  port.role = isHostModuleType(stringValue(node_json, "module_type")) ? "host" : "dai";
  port.resource_ref = port.port_id;
  port.pcm_id = stringValue(params, "stream_name");
  port.dai_id = stringValue(params, "link_name");
  port.aif_role = stringValue(params, "role_label", stringValue(params, "aif_role"));
  port.transport = stringValue(params, "dai_type");
  port.hw_id = stringValue(params, "device_id");
  const std::string direction = stringValue(params, "direction");
  if (port.hw_dir.empty() && direction == "playback") port.hw_dir = "out";
  if (port.hw_dir.empty() && direction == "capture") port.hw_dir = "in";
  if (params.has("dai_index") && !params.at("dai_index").isNull()) {
    port.has_dai_index = true;
    port.dai_index = uintValue(params, "dai_index", 0);
  }
  applyCapabilitiesToPort(port, params, default_rate);
  return port;
}

ResourceCatalogInfo parseResourceCatalog(const JsonValue& root) {
  ResourceCatalogInfo catalog;
  if (!root.isObject()) return catalog;
  const JsonValue* catalog_json = optionalObject(root, "resource_catalog");
  if (catalog_json == nullptr) catalog_json = optionalObject(root, "hardware_hints");
  if (catalog_json == nullptr) return catalog;

  if (catalog_json->has("compute") && catalog_json->at("compute").isArray()) {
    for (const auto& item : catalog_json->at("compute").asArray()) {
      CoreResourceInfo core;
      core.resource_id = stringValue(item, "id", stringValue(item, "core_id"));
      if (core.resource_id.empty()) throw std::runtime_error("resource_catalog compute item missing id");
      core.core = uintValueAny(item, {"index", "core"}, 0);
      catalog.cores.emplace(core.resource_id, std::move(core));
    }
  }
  if (catalog_json->has("cores") && catalog_json->at("cores").isArray()) {
    for (const auto& item : catalog_json->at("cores").asArray()) {
      CoreResourceInfo core;
      core.resource_id = stringValue(item, "id", stringValue(item, "core_id"));
      if (core.resource_id.empty()) throw std::runtime_error("resource_catalog core item missing id");
      core.core = uintValueAny(item, {"index", "core"}, 0);
      catalog.cores.emplace(core.resource_id, std::move(core));
    }
  }
  return catalog;
}

SofSchedulingInfo parseScheduling(const JsonValue& pipeline_json, const ResourceCatalogInfo& catalog) {
  SofSchedulingInfo scheduling;
  if (pipeline_json.has("frame") && pipeline_json.at("frame").isObject()) {
    const auto& frame = pipeline_json.at("frame");
    const uint32_t block_ms = uintValue(frame, "block_ms", 0);
    if (block_ms != 0) scheduling.period_us = block_ms * 1000;
  }
  if (pipeline_json.has("sof") && pipeline_json.at("sof").isObject()) {
    const auto& sof = pipeline_json.at("sof");
    scheduling.period_us = uintValueAny(sof, {"period_us", "sched_period_us"}, scheduling.period_us);
    scheduling.priority = uintValueAny(sof, {"priority", "sched_priority"}, scheduling.priority);
    scheduling.core = uintValueAny(sof, {"core", "core_id", "sched_core"}, scheduling.core);
    scheduling.time_domain = uintValueAny(sof, {"time_domain", "sched_time_domain"}, scheduling.time_domain);
  }
  if (pipeline_json.has("runtime") && pipeline_json.at("runtime").isObject()) {
    const auto& runtime = pipeline_json.at("runtime");
    scheduling.period_us = uintValueAny(runtime, {"period_us", "schedule_period_us"}, scheduling.period_us);
    scheduling.priority = uintValueAny(runtime, {"priority", "schedule_priority"}, scheduling.priority);
    const std::string core_ref = stringValue(runtime, "core_ref");
    if (!core_ref.empty()) {
      const auto core_it = catalog.cores.find(core_ref);
      if (core_it == catalog.cores.end()) throw std::runtime_error("pipeline runtime references unknown core_ref: " + core_ref);
      scheduling.core = core_it->second.core;
    }
    scheduling.core = uintValueAny(runtime, {"core_index", "core"}, scheduling.core);
    const std::string clock = lowerAscii(stringValue(runtime, "clock", stringValue(runtime, "schedule_clock")));
    if (clock == "timer" || clock == "wall") scheduling.time_domain = 1;
    if (clock == "dma" || clock == "dai") scheduling.time_domain = 0;
  }
  return scheduling;
}

std::string moduleUuidValue(const JsonValue& object) {
  std::string uuid = stringValue(object, "module_uuid");
  if (uuid.empty()) uuid = stringValue(object, "uuid");
  if (uuid.empty() && object.has("sof") && object.at("sof").isObject()) {
    uuid = stringValue(object.at("sof"), "uuid");
  }
  if (uuid.empty()) return {};
  uuid = lowerAscii(trim(uuid));
  (void)uuidBytes(uuid);
  return uuid;
}

ModuleTypeInfo parseModuleType(const JsonValue& object) {
  ModuleTypeInfo type;
  type.type_id = stringValue(object, "type_id");
  if (type.type_id.empty()) throw std::runtime_error("module type missing type_id");
  type.source_json = object.dump();
  type.category = stringValue(object, "category");
  type.module_class = stringValue(object, "module_class");
  type.module_uuid = moduleUuidValue(object);
  if (type.module_class == "MODULE_ADAPTER" && type.module_uuid.empty()) {
    throw std::runtime_error("MODULE_ADAPTER module type must declare module_uuid: " + type.type_id);
  }
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

bool fileExists(drivers::filesystem::IFileSystemDriver& fs, const std::string& path) {
  drivers::filesystem::FileInfo info;
  return fs.stat(path, info).ok() && !info.directory;
}

std::string resolveImportPath(drivers::filesystem::IFileSystemDriver& fs,
                              const std::string& input_path,
                              const std::string& import_path) {
  if (import_path.empty()) throw std::runtime_error("module catalog import path is empty");
  const std::filesystem::path import_fs_path(import_path);
  if (import_fs_path.is_absolute()) return import_fs_path.lexically_normal().string();

  std::vector<std::string> candidates;
  candidates.push_back(fs.absolutePath(import_path));

  auto input_dir = std::filesystem::path(input_path).parent_path();
  if (!input_dir.empty()) {
    while (!input_dir.empty()) {
      candidates.push_back((input_dir / import_fs_path).lexically_normal().string());
      const auto parent = input_dir.parent_path();
      if (parent == input_dir) break;
      input_dir = parent;
    }
  }

  for (const auto& candidate : candidates) {
    if (fileExists(fs, candidate)) return candidate;
  }
  return candidates.empty() ? import_path : candidates.front();
}

std::vector<JsonValue> loadImportedModuleTypes(drivers::filesystem::IFileSystemDriver& fs,
                                               const JsonValue& root,
                                               const std::string& input_path) {
  std::vector<JsonValue> module_types;
  if (!root.isObject() || !root.has("imports") || root.at("imports").isNull()) return module_types;
  const auto& imports = requiredArray(root, "imports").asArray();
  for (const auto& item : imports) {
    if (!item.isObject()) throw std::runtime_error("imports entries must be objects");
    if (stringValue(item, "kind") != "module_catalog") continue;
    const auto import_path = resolveImportPath(fs, input_path, stringValue(item, "path"));
    std::string catalog_text;
    auto status = readText(fs, import_path, catalog_text);
    if (!status.ok()) throw std::runtime_error(status.message());
    const auto catalog = audio_studio::rpc::parseJson(catalog_text);
    for (const auto& module_type : requiredArray(catalog, "module_types").asArray()) {
      module_types.push_back(module_type);
    }
  }
  return module_types;
}

ProjectIr parseProject(const JsonValue& root,
                       const std::string& project_name,
                       const std::vector<JsonValue>& imported_module_types) {
  ProjectIr ir;
  ir.project_name = project_name.empty() ? "a2" : project_name;
  const auto resource_catalog = parseResourceCatalog(root);

  for (const auto& item : imported_module_types) {
    auto type = parseModuleType(item);
    if (!ir.module_types.emplace(type.type_id, std::move(type)).second) {
      throw std::runtime_error("duplicate imported module type");
    }
  }

  for (const auto& item : requiredArray(root, "module_types").asArray()) {
    auto type = parseModuleType(item);
    if (!ir.module_types.emplace(type.type_id, std::move(type)).second) {
      throw std::runtime_error("duplicate module type");
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
    pipe.scheduling = parseScheduling(item, resource_catalog);

    for (const auto& node_json : requiredArray(item, "nodes").asArray()) {
      NodeInfo node;
      node.node_id = stringValue(node_json, "node_id");
      node.inst_id = node.node_id;
      node.name = stringValue(node_json, "name", node.node_id);
      node.module_type = stringValue(node_json, "module_type");
      node.source_json = node_json.dump();
      node.params_json = jsonObjectValueMap(node_json, "params");
      if (node.node_id.empty()) throw std::runtime_error("pipeline node missing node_id in " + pipe.pipe_id);
      if (node.module_type.empty()) throw std::runtime_error("pipeline node missing module_type: " + pipe.pipe_id + "." + node.node_id);
      const auto type_it = ir.module_types.find(node.module_type);
      if (type_it == ir.module_types.end()) throw std::runtime_error("pipeline node references unknown module_type: " + node.module_type);
      for (const auto& param : node.params_json) {
        const auto found = std::any_of(type_it->second.parameters.begin(), type_it->second.parameters.end(),
                                       [&](const auto& candidate) { return candidate.param_id == param.first; });
        if (!found) {
          throw std::runtime_error("node parameter is not declared by module_type: " +
                                   pipe.pipe_id + "." + node.node_id + "." + param.first);
        }
      }
      if (isHostModuleType(node.module_type) || isDaiModuleType(node.module_type)) {
        JsonValue empty_params = JsonValue::object();
        const JsonValue* params_ptr = optionalObject(node_json, "params");
        const JsonValue& params = params_ptr == nullptr ? empty_params : *params_ptr;
        node.endpoint_present = true;
        node.endpoint = parseEndpointNode(node_json, params, pipe.sample_rate);
      }
      node.widget_name = pipe.pipe_id + "." + node.node_id;
      pipe.nodes.push_back(std::move(node));
    }
    for (const auto& node : pipe.nodes) {
      if (!node.endpoint_present) continue;
      auto endpoint = node.endpoint;
      if (endpoint.port_id.empty()) endpoint.port_id = node.inst_id;
      const auto duplicate = std::any_of(pipe.ports.begin(), pipe.ports.end(),
                                         [&](const auto& port) { return port.port_id == endpoint.port_id; });
      if (!duplicate) pipe.ports.push_back(std::move(endpoint));
    }
    pipe.channels_min = minChannelsFromPortInfos(pipe.ports);
    pipe.channels_max = maxChannelsFromPortInfos(pipe.ports);
    pipe.sample_bits = sampleBitsFromPortInfos(pipe.ports);
    pipe.sample_rate = sampleRateFromPortInfos(pipe.ports, pipe.sample_rate);

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

module_config::ModuleConfigRequest makeModuleConfigRequest(const ProjectIr& ir,
                                                           const PipelineInfo& pipe,
                                                           const NodeInfo& node,
                                                           const ParameterInfo* param,
                                                           std::string values_json,
                                                           std::string preset_id = {},
                                                           std::string preset_entry_json = {}) {
  const auto& type = moduleTypeForNode(ir, node);

  module_config::ModuleConfigRequest request;
  request.module_type = node.module_type;
  request.pipeline_id = pipe.pipe_id;
  request.node_id = node.node_id;
  request.instance_id = node.inst_id;
  request.parameter_id = param == nullptr ? std::string{} : param->param_id;
  request.preset_id = std::move(preset_id);
  request.module_type_json = type.source_json;
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

std::string valueJsonForNodeParam(const NodeInfo& node, const ParameterInfo& param) {
  const auto it = node.params_json.find(param.param_id);
  return it == node.params_json.end() ? param.default_json : it->second;
}

std::string singleParamValuesJson(const NodeInfo& node, const ParameterInfo& param) {
  return "{" + quote(param.param_id) + ":" + valueJsonForNodeParam(node, param) + "}";
}

std::string allDefaultValuesJson(const ModuleTypeInfo& type, const NodeInfo& node) {
  std::ostringstream json;
  json << "{";
  for (size_t i = 0; i < type.parameters.size(); ++i) {
    if (i != 0) json << ",";
    json << quote(type.parameters[i].param_id) << ":" << valueJsonForNodeParam(node, type.parameters[i]);
  }
  json << "}";
  return json.str();
}

bool usesAggregateSofTopologyControl(const std::string& module_type) {
  return module_type == "filter.channel_remap" || module_type == "filter.chremap" ||
         module_type == "delay.line" || module_type == "delay.delay_line" ||
         module_type == "mix.fader_balance" || module_type == "filter.dsp_filter";
}

std::string aggregateTopologyParamId(const ModuleTypeInfo& type) {
  if (type.parameters.empty()) return {};
  return type.parameters.front().param_id;
}

std::string aggregateTopologyControlLabel(const std::string& module_type) {
  if (module_type == "filter.channel_remap" || module_type == "filter.chremap") return "Channel Remap";
  if (module_type == "delay.line" || module_type == "delay.delay_line") return "Delay Line";
  if (module_type == "mix.fader_balance") return "Fader Balance";
  if (module_type == "filter.dsp_filter") return "DSP Filter";
  return "Config";
}

std::string aggregateTopologyControlName(const PipelineInfo& pipeline, const NodeInfo& node) {
  return pipeline.pipe_id + " " + node.node_id + " " + aggregateTopologyControlLabel(node.module_type);
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

  auto request = makeModuleConfigRequest(ir, pipe, node, &param, singleParamValuesJson(node, param));
  auto status = runtime ? handler->packRuntimeParam(request, blob) : handler->packInstallConfig(request, blob);
  return fromModuleConfigStatus(status);
}

Status packAggregateTopologyDefault(const ProjectIr& ir,
                                    const ModuleConfigRegistry& module_configs,
                                    const PipelineInfo& pipe,
                                    const NodeInfo& node,
                                    module_config::ModuleConfigBlob& blob) {
  const auto* handler = module_configs.find(node.module_type);
  if (handler == nullptr) return Status::unavailable("no module config handler registered for module_type: " + node.module_type);

  const auto& type = moduleTypeForNode(ir, node);
  auto request = makeModuleConfigRequest(ir, pipe, node, nullptr, allDefaultValuesJson(type, node));
  request.parameter_id = "config";
  request.parameter_json = R"({"param_id":"config","param_name":"Config","value_type":"bytes"})";
  auto status = handler->packInstallConfig(request, blob);
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
      const bool aggregate_topology = usesAggregateSofTopologyControl(node.module_type);
      const std::string aggregate_param_id = aggregate_topology ? aggregateTopologyParamId(type) : std::string{};
      module_config::ModuleConfigBlob aggregate_blob;
      if (aggregate_topology) {
        auto status = packAggregateTopologyDefault(ir, module_configs, pipe, node, aggregate_blob);
        if (!status.ok()) return status;
      }
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
          control.emit_topology = !aggregate_topology || control.param_id == aggregate_param_id;
          if (aggregate_topology && control.emit_topology) {
            control.topology_control_name = aggregateTopologyControlName(pipe, node);
            control.topology_default_payload = aggregate_blob.data;
          }
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
      json << "\"resource_ref\":" << quote(port.resource_ref) << ",";
      json << "\"pcm_id\":" << quote(port.pcm_id) << ",";
      json << "\"dai_id\":" << quote(port.dai_id) << ",";
      json << "\"aif_role\":" << quote(port.aif_role) << ",";
      json << "\"transport\":" << quote(port.transport) << ",";
      json << "\"hw_id\":" << quote(port.hw_id) << ",";
      json << "\"hw_dir\":" << quote(port.hw_dir) << ",";
      json << "\"dai_index\":" << (port.has_dai_index ? port.dai_index : 0) << ",";
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
  if (!control.emit_topology) return {};

  std::ostringstream out;
  const std::string topology_control_name = control.topology_control_name.empty()
      ? control.control_name
      : control.topology_control_name;
  const auto& topology_payload = control.topology_default_payload.empty()
      ? control.default_payload
      : control.topology_default_payload;
  const std::string name = topologyQuote(topology_control_name);
  if (control.config_format == "sof-ipc3-bytes-v1") {
    const std::string data_name = topology_control_name + " Data";
    out << "SectionData." << topologyQuote(data_name) << " {\n";
    out << "  bytes " << topologyQuote(hexBytes(topology_payload)) << "\n";
    out << "}\n";
    out << "SectionControlBytes." << name << " {\n";
    out << "  index \"1\"\n";
    out << "  ops.\"ctl\" { info \"bytes\" }\n";
    out << "  extops.\"extctl\" { get \"258\" put \"258\" }\n";
    out << "  base \"0x00\"\n";
    out << "  num_regs \"0x00\"\n";
    out << "  mask \"0x00\"\n";
    out << "  max " << topologyQuote(std::to_string(std::max<size_t>(1, topology_payload.size()))) << "\n";
    out << "  access [ tlv_write tlv_read tlv_callback ]\n";
    out << "  data [ " << topologyQuote(data_name) << " ]\n";
    out << "}\n\n";
    return out.str();
  }
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
    max_value = std::max<int64_t>(1, static_cast<int64_t>(std::llround(span / std::max(0.000001, control.range.step))));
  } else if (control.value_type == "uint8") {
    max_value = 255;
  } else if (control.value_type == "uint16") {
    max_value = 65535;
  }

  if (control.range.present && !is_bool) {
    const std::string tlv_name = topologyQuote(control.control_name + " TLV");
    out << "SectionTLV." << tlv_name << " {\n";
    out << "  scale { min " << topologyQuote(std::to_string(static_cast<int64_t>(std::llround(control.range.min * 100.0))))
        << " step " << topologyQuote(std::to_string(static_cast<int64_t>(std::llround(control.range.step * 100.0)))) << " mute \"0\" }\n";
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
  if (isHostModuleType(node.module_type)) return pipeline.direction == "capture" ? "aif_out" : "aif_in";
  if (isDaiModuleType(node.module_type)) return pipeline.direction == "capture" ? "dai_out" : "dai_in";
  if (node.module_type == "gain.volume") return "pga";
  return "mixer";
}

const PortInfo* findPort(const PipelineInfo& pipeline, const NodeInfo& node) {
  (void)pipeline;
  if (node.endpoint_present) return &node.endpoint;
  return nullptr;
}

std::string sanitizeName(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (const unsigned char ch : value) {
    if (std::isalnum(ch)) out.push_back(static_cast<char>(ch));
    else out.push_back('_');
  }
  if (out.empty()) out = "item";
  return out;
}

std::string sofSectionBase(const PipelineInfo& pipeline, const NodeInfo& node) {
  return sanitizeName(pipeline.pipe_id + "_" + node.node_id);
}

std::string sofPipelineBase(const PipelineInfo& pipeline) {
  return sanitizeName(pipeline.pipe_id + "_SCHED");
}

std::string daiTypeForPort(const PortInfo& port) {
  if (port.transport == "file_io" || port.transport == "file-io" || port.transport == "file_io_dai") return "FILE_IO";
  if (port.transport == "i2s") return "VSI_I2S";
  if (port.transport == "qtdm") return "VCA_QTDM";
  if (port.transport == "qi2s") return "VCA_QI2S";
  if (port.transport == "pdm") return "VCA_PDM";
  return "VSI_TDM";
}

uint32_t daiIndexForPort(const PortInfo& port, uint32_t fallback) {
  if (port.has_dai_index) return port.dai_index;
  std::string digits;
  for (const unsigned char ch : port.hw_id) {
    if (std::isdigit(ch)) digits.push_back(static_cast<char>(ch));
  }
  if (digits.empty()) return fallback;
  return static_cast<uint32_t>(std::stoul(digits));
}

bool isDaiPortWidget(const NodeInfo& node, const PipelineInfo& pipeline) {
  (void)pipeline;
  if (isDaiModuleType(node.module_type)) return true;
  return false;
}

bool isHostPortWidget(const NodeInfo& node, const PipelineInfo& pipeline) {
  (void)pipeline;
  if (isHostModuleType(node.module_type)) return true;
  return false;
}

std::string sofWidgetType(const NodeInfo& node, const PipelineInfo& pipeline) {
  if (isHostPortWidget(node, pipeline) || isDaiPortWidget(node, pipeline)) {
    if (isDaiPortWidget(node, pipeline)) return pipeline.direction == "capture" ? "dai_out" : "dai_in";
    return pipeline.direction == "capture" ? "aif_out" : "aif_in";
  }
  if (node.module_type == "gain.volume") return "pga";
  if (node.module_type == "mix.mixer") return "mixer";
  if (node.module_type == "rate.src") return "src";
  if (node.module_type == "rate.asrc") return "asrc";
  return "effect";
}

const ModuleTypeInfo* moduleTypeForWidget(const ProjectIr& ir, const NodeInfo& node) {
  if (node.module_type.empty()) return nullptr;
  const auto it = ir.module_types.find(node.module_type);
  if (it == ir.module_types.end()) throw std::runtime_error("node module type not found: " + node.module_type);
  return &it->second;
}

std::string sofRegistryNameForNode(const NodeInfo& node, const PipelineInfo& pipeline) {
  if (isHostPortWidget(node, pipeline) || isDaiPortWidget(node, pipeline)) {
    return isDaiPortWidget(node, pipeline) ? "dai" : "host";
  }
  if (node.module_type == "gain.volume") return "volume";
  if (node.module_type == "mix.mixer") return "mixer";
  if (node.module_type == "route.mux") return "mux";
  if (node.module_type == "route.selector") return "selector";
  if (node.module_type == "rate.src") return "src";
  if (node.module_type == "rate.asrc") return "asrc";
  if (node.module_type == "filter.channel_remap" || node.module_type == "filter.chremap") return "chremap";
  if (node.module_type == "filter.dcblock") return "dcblock";
  if (node.module_type == "delay.line" || node.module_type == "delay.delay_line") return "delay_line";
  if (node.module_type == "mix.fader_balance") return "fader_balance";
  if (node.module_type == "filter.dsp_filter") return "dsp_filter";
  if (node.module_type == "eq.iir") return "eq_iir";
  if (node.module_type == "eq.fir") return "eq_fir";
  if (node.module_type == "dyn.drc") return "drc";
  if (node.module_type == "dyn.multiband_drc") return "multiband_drc";
  if (node.module_type == "filter.crossover") return "crossover";
  if (node.module_type == "mix.up_down_mixer") return "up_down_mixer";
  if (node.module_type == "voice.kpb") return "kpb";
  throw std::runtime_error("No SOF UUID registry mapping for module_type: " + node.module_type);
}

std::string sofUuidForNode(const ProjectIr& ir, const NodeInfo& node, const PipelineInfo& pipeline) {
  const auto* type = moduleTypeForWidget(ir, node);
  if (type != nullptr) {
    if (!type->module_uuid.empty()) return type->module_uuid;
    if (type->module_class == "MODULE_ADAPTER") {
      throw std::runtime_error("MODULE_ADAPTER module type must declare module_uuid: " + type->type_id);
    }
  }
  return sofUuidFromRegistry(sofRegistryNameForNode(node, pipeline));
}

std::string processTypeForNode(const NodeInfo& node) {
  if (node.module_type == "filter.dcblock") return "DCBLOCK";
  if (node.module_type == "route.mux") return "MUX";
  if (node.module_type == "route.selector") return "CHAN_SELECTOR";
  if (node.module_type == "filter.channel_remap" || node.module_type == "filter.chremap") return "CHAN_REMAP";
  if (node.module_type == "delay.line" || node.module_type == "delay.delay_line") return "DELAY_LINE";
  if (node.module_type == "mix.fader_balance") return "FADER_BALANCE";
  if (node.module_type == "filter.dsp_filter") return "DSP_FILTER";
  if (node.module_type == "eq.iir") return "EQIIR";
  if (node.module_type == "eq.fir") return "EQFIR";
  if (node.module_type == "dyn.drc") return "DRC";
  if (node.module_type == "dyn.multiband_drc") return "MULTIBAND_DRC";
  if (node.module_type == "filter.crossover") return "CROSSOVER";
  if (node.module_type == "voice.kpb") return "KPB";
  return macroToken(node.module_type);
}

WidgetControlRefs controlsForNode(const ProjectIr& ir, const PipelineInfo& pipeline, const NodeInfo& node) {
  WidgetControlRefs refs;
  for (const auto& control : ir.controls) {
    if (control.pipe_id == pipeline.pipe_id && control.node_id == node.node_id && control.emit_topology) {
      const auto& topology_control_name = control.topology_control_name.empty()
          ? control.control_name
          : control.topology_control_name;
      if (control.config_format == "sof-ipc3-bytes-v1") {
        refs.bytes.push_back(topology_control_name);
      } else if (control.value_type == "enum") {
        refs.enumerated.push_back(topology_control_name);
      } else {
        refs.mixer.push_back(topology_control_name);
      }
    }
  }
  return refs;
}

uint32_t periodsSinkForNode(const NodeInfo& node, const PipelineInfo& pipeline) {
  if (isHostPortWidget(node, pipeline)) {
    return pipeline.direction == "playback" ? 4 : 0;
  }
  if (isDaiPortWidget(node, pipeline)) {
    return pipeline.direction == "playback" ? 0 : 2;
  }
  return 2;
}

uint32_t periodsSourceForNode(const NodeInfo& node, const PipelineInfo& pipeline) {
  if (isHostPortWidget(node, pipeline)) {
    return pipeline.direction == "capture" ? 2 : 0;
  }
  if (isDaiPortWidget(node, pipeline)) {
    return pipeline.direction == "playback" ? 2 : 0;
  }
  if (node.module_type == "filter.channel_remap" || node.module_type == "filter.chremap" ||
      node.module_type == "delay.line" || node.module_type == "delay.delay_line" ||
      node.module_type == "filter.dsp_filter") {
    return 3;
  }
  return 2;
}

uint32_t pipelinePeriodFrames(const PipelineInfo& pipeline) {
  const uint64_t product = static_cast<uint64_t>(pipeline.sample_rate) * pipeline.scheduling.period_us;
  return static_cast<uint32_t>((product + 1000000ull - 1ull) / 1000000ull);
}

uint32_t sampleBytesForSampleBits(uint32_t sample_bits) {
  return sample_bits <= 16 ? 2u : 4u;
}

uint32_t edgeBufferPeriods(const NodeInfo& from, const NodeInfo& to, const PipelineInfo& pipeline) {
  uint32_t periods = periodsSourceForNode(from, pipeline);
  if (periods == 0) periods = periodsSinkForNode(from, pipeline);
  if (periods == 0) periods = periodsSinkForNode(to, pipeline);
  return periods == 0 ? 2u : periods;
}

uint32_t edgeBufferCaps(const NodeInfo& from, const NodeInfo& to, const PipelineInfo& pipeline) {
  constexpr uint32_t kComponentCaps = 65; // SOF_MEM_CAPS_RAM | SOF_MEM_CAPS_CACHE
  constexpr uint32_t kEndpointCaps = 97;  // SOF_MEM_CAPS_RAM | SOF_MEM_CAPS_DMA | SOF_MEM_CAPS_CACHE
  if (isHostPortWidget(from, pipeline) || isHostPortWidget(to, pipeline) ||
      isDaiPortWidget(from, pipeline) || isDaiPortWidget(to, pipeline)) {
    return kEndpointCaps;
  }
  return kComponentCaps;
}

std::string inferredBufferName(const PipelineInfo& pipeline, size_t edge_index) {
  return pipeline.pipe_id + ".BUF" + std::to_string(edge_index);
}

std::string inferredBufferBase(const PipelineInfo& pipeline, size_t edge_index) {
  return sanitizeName(pipeline.pipe_id + "_BUF" + std::to_string(edge_index));
}

InferredBufferInfo inferBufferForEdge(const PipelineInfo& pipeline,
                                      const NodeInfo& from,
                                      const NodeInfo& to,
                                      size_t edge_index) {
  const uint32_t periods = edgeBufferPeriods(from, to, pipeline);
  const uint32_t channels = std::max<uint32_t>(1u, pipeline.channels_max);
  const uint32_t period_frames = std::max<uint32_t>(1u, pipelinePeriodFrames(pipeline));
  const uint32_t sample_bytes = sampleBytesForSampleBits(pipeline.sample_bits);
  InferredBufferInfo buffer;
  buffer.edge_index = edge_index;
  buffer.widget_name = inferredBufferName(pipeline, edge_index);
  buffer.size_bytes = periods * sample_bytes * channels * period_frames;
  buffer.caps = edgeBufferCaps(from, to, pipeline);
  return buffer;
}

std::string pcmFormatForSampleBits(uint32_t sample_bits) {
  if (sample_bits <= 16) return "S16_LE";
  if (sample_bits <= 24) return "S24_LE";
  return "S32_LE";
}

void emitSofVendorTokens(std::ostringstream& out) {
  out << "SectionVendorTokens.\"sof_buffer_tokens\" {\n"
      << "  SOF_TKN_BUF_SIZE \"100\"\n"
      << "  SOF_TKN_BUF_CAPS \"101\"\n"
      << "}\n\n";
  out << "SectionVendorTokens.\"sof_dai_tokens\" {\n"
      << "  SOF_TKN_DAI_TYPE \"154\"\n"
      << "  SOF_TKN_DAI_INDEX \"155\"\n"
      << "  SOF_TKN_DAI_DIRECTION \"156\"\n"
      << "}\n\n";
  out << "SectionVendorTokens.\"sof_sched_tokens\" {\n"
      << "  SOF_TKN_SCHED_PERIOD \"200\"\n"
      << "  SOF_TKN_SCHED_PRIORITY \"201\"\n"
      << "  SOF_TKN_SCHED_MIPS \"202\"\n"
      << "  SOF_TKN_SCHED_CORE \"203\"\n"
      << "  SOF_TKN_SCHED_FRAMES \"204\"\n"
      << "  SOF_TKN_SCHED_TIME_DOMAIN \"205\"\n"
      << "  SOF_TKN_SCHED_DYNAMIC_PIPELINE \"206\"\n"
      << "}\n\n";
  out << "SectionVendorTokens.\"sof_comp_tokens\" {\n"
      << "  SOF_TKN_COMP_PERIOD_SINK_COUNT \"400\"\n"
      << "  SOF_TKN_COMP_PERIOD_SOURCE_COUNT \"401\"\n"
      << "  SOF_TKN_COMP_FORMAT \"402\"\n"
      << "  SOF_TKN_COMP_CORE_ID \"404\"\n"
      << "  SOF_TKN_COMP_UUID \"405\"\n"
      << "}\n\n";
  out << "SectionVendorTokens.\"sof_src_tokens\" {\n"
      << "  SOF_TKN_SRC_RATE_IN \"300\"\n"
      << "  SOF_TKN_SRC_RATE_OUT \"301\"\n"
      << "}\n\n";
  out << "SectionVendorTokens.\"sof_asrc_tokens\" {\n"
      << "  SOF_TKN_ASRC_RATE_IN \"320\"\n"
      << "  SOF_TKN_ASRC_RATE_OUT \"321\"\n"
      << "  SOF_TKN_ASRC_ASYNCHRONOUS_MODE \"322\"\n"
      << "  SOF_TKN_ASRC_OPERATION_MODE \"323\"\n"
      << "}\n\n";
  out << "SectionVendorTokens.\"sof_process_tokens\" {\n"
      << "  SOF_TKN_PROCESS_TYPE \"900\"\n"
      << "}\n\n";
  out << "SectionVendorTokens.\"sof_vsi_tdm_tokens\" {\n"
      << "  SOF_TKN_VSI_TDM_MCLK_ID \"4400\"\n"
      << "  SOF_TKN_VSI_TDM_SAMPLE_WIDTH \"4401\"\n"
      << "  SOF_TKN_VSI_TDM_STEREO \"4402\"\n"
      << "  SOF_TKN_VSI_TDM_DELAY_MODE \"4403\"\n"
      << "  SOF_TKN_VSI_TDM_EDGE_MODE \"4404\"\n"
      << "  SOF_TKN_VSI_TDM_WS_POLARITY \"4405\"\n"
      << "}\n\n";
}

void emitTupleData(std::ostringstream& out, const std::string& data_name, const std::string& tuple_name) {
  out << "SectionData." << topologyQuote(data_name) << " {\n";
  out << "  tuples " << topologyQuote(tuple_name) << "\n";
  out << "}\n\n";
}

void emitWordTuples(std::ostringstream& out,
                    const std::string& tuple_name,
                    const std::string& token_set,
                    const std::vector<std::pair<std::string, uint32_t>>& values) {
  out << "SectionVendorTuples." << topologyQuote(tuple_name) << " {\n";
  out << "  tokens " << topologyQuote(token_set) << "\n";
  out << "  tuples.\"word\" {\n";
  for (const auto& item : values) {
    out << "    " << item.first << " " << topologyQuote(std::to_string(item.second)) << "\n";
  }
  out << "  }\n";
  out << "}\n";
}

void emitStringTuples(std::ostringstream& out,
                      const std::string& tuple_name,
                      const std::string& token_set,
                      const std::vector<std::pair<std::string, std::string>>& values) {
  out << "SectionVendorTuples." << topologyQuote(tuple_name) << " {\n";
  out << "  tokens " << topologyQuote(token_set) << "\n";
  out << "  tuples.\"string\" {\n";
  for (const auto& item : values) {
    out << "    " << item.first << " " << topologyQuote(item.second) << "\n";
  }
  out << "  }\n";
  out << "}\n";
}

void emitUuidTuples(std::ostringstream& out, const std::string& tuple_name, const std::string& uuid) {
  out << "SectionVendorTuples." << topologyQuote(tuple_name) << " {\n";
  out << "  tokens \"sof_comp_tokens\"\n";
  out << "  tuples.\"uuid\" {\n";
  out << "    SOF_TKN_COMP_UUID " << topologyQuote(colonHexBytes(sofUuidTokenBytes(uuid))) << "\n";
  out << "  }\n";
  out << "}\n";
}

void emitPipelineScheduler(std::ostringstream& out, const PipelineInfo& pipeline) {
  const auto base = sofPipelineBase(pipeline);
  const auto tuples = base + "_tuples";
  const auto data = base + "_data";
  emitWordTuples(out, tuples, "sof_sched_tokens", {
    {"SOF_TKN_SCHED_PERIOD", pipeline.scheduling.period_us},
    {"SOF_TKN_SCHED_PRIORITY", pipeline.scheduling.priority},
    {"SOF_TKN_SCHED_MIPS", 50000},
    {"SOF_TKN_SCHED_CORE", pipeline.scheduling.core},
    {"SOF_TKN_SCHED_FRAMES", 0},
    {"SOF_TKN_SCHED_TIME_DOMAIN", pipeline.scheduling.time_domain},
    {"SOF_TKN_SCHED_DYNAMIC_PIPELINE", 0},
  });
  emitTupleData(out, data, tuples);
  out << "SectionWidget." << topologyQuote(pipeline.pipe_id + ".SCHED") << " {\n";
  out << "  index " << topologyQuote(std::to_string(pipeline.pcm_index + 1)) << "\n";
  out << "  type \"scheduler\"\n";
  out << "  no_pm \"true\"\n";
  out << "  stream_name " << topologyQuote(pipeline.name) << "\n";
  out << "  data [ " << topologyQuote(data) << " ]\n";
  out << "}\n\n";
}

void emitDaiScheduler(std::ostringstream& out, const PipelineInfo& pipeline, const NodeInfo& node) {
  const auto base = sofSectionBase(pipeline, node) + "_SCHED";
  const auto tuples = base + "_tuples";
  const auto data = base + "_data";
  emitWordTuples(out, tuples, "sof_sched_tokens", {
    {"SOF_TKN_SCHED_PERIOD", pipeline.scheduling.period_us},
    {"SOF_TKN_SCHED_PRIORITY", pipeline.scheduling.priority},
    {"SOF_TKN_SCHED_MIPS", 5000},
    {"SOF_TKN_SCHED_CORE", pipeline.scheduling.core},
    {"SOF_TKN_SCHED_FRAMES", 0},
    {"SOF_TKN_SCHED_TIME_DOMAIN", 0},
    {"SOF_TKN_SCHED_DYNAMIC_PIPELINE", 0},
  });
  emitTupleData(out, data, tuples);
  out << "SectionWidget." << topologyQuote(node.widget_name + ".SCHED") << " {\n";
  out << "  index " << topologyQuote(std::to_string(pipeline.pcm_index + 1)) << "\n";
  out << "  type \"scheduler\"\n";
  out << "  no_pm \"true\"\n";
  out << "  stream_name " << topologyQuote(node.widget_name) << "\n";
  out << "  data [ " << topologyQuote(data) << " ]\n";
  out << "}\n\n";
}

void emitInferredBuffer(std::ostringstream& out,
                        const PipelineInfo& pipeline,
                        const InferredBufferInfo& buffer) {
  const std::string base = inferredBufferBase(pipeline, buffer.edge_index);
  const std::string buffer_tuple = base + "_tuples";
  const std::string buffer_data = base + "_data";
  const std::string comp_tuple = base + "_comp_tuples";
  const std::string comp_data = base + "_comp_data";

  emitWordTuples(out, buffer_tuple, "sof_buffer_tokens", {
    {"SOF_TKN_BUF_SIZE", buffer.size_bytes},
    {"SOF_TKN_BUF_CAPS", buffer.caps},
  });
  emitTupleData(out, buffer_data, buffer_tuple);
  emitWordTuples(out, comp_tuple, "sof_comp_tokens", {
    {"SOF_TKN_COMP_CORE_ID", pipeline.scheduling.core},
  });
  emitTupleData(out, comp_data, comp_tuple);

  out << "SectionWidget." << topologyQuote(buffer.widget_name) << " {\n";
  out << "  index " << topologyQuote(std::to_string(pipeline.pcm_index + 1)) << "\n";
  out << "  type \"buffer\"\n";
  out << "  no_pm \"true\"\n";
  out << "  data [\n";
  out << "    " << topologyQuote(buffer_data) << "\n";
  out << "    " << topologyQuote(comp_data) << "\n";
  out << "  ]\n";
  out << "}\n\n";
}

void emitInferredBuffers(std::ostringstream& out, const PipelineInfo& pipeline) {
  for (size_t i = 0; i < pipeline.edges.size(); ++i) {
    const auto* from = findNode(pipeline, pipeline.edges[i].from_node);
    const auto* to = findNode(pipeline, pipeline.edges[i].to_node);
    if (from == nullptr || to == nullptr) continue;
    emitInferredBuffer(out, pipeline, inferBufferForEdge(pipeline, *from, *to, i));
  }
}

void emitDaiSchedulers(std::ostringstream& out, const PipelineInfo& pipeline) {
  for (const auto& node : pipeline.nodes) {
    if (isDaiPortWidget(node, pipeline)) {
      emitDaiScheduler(out, pipeline, node);
    }
  }
}

void emitSofWidgetData(std::ostringstream& out, const ProjectIr& ir, const PipelineInfo& pipeline, const NodeInfo& node) {
  const std::string base = sofSectionBase(pipeline, node);
  const std::string uuid_tuple = base + "_uuid_tuples";
  const std::string uuid_data = base + "_uuid_data";
  const std::string word_tuple = base + "_comp_tuples_w";
  const std::string word_data = base + "_comp_data_w";
  const std::string string_tuple = base + "_comp_tuples_s";
  const std::string string_data = base + "_comp_data_s";
  emitUuidTuples(out, uuid_tuple, sofUuidForNode(ir, node, pipeline));
  emitTupleData(out, uuid_data, uuid_tuple);
  emitWordTuples(out, word_tuple, "sof_comp_tokens", {
    {"SOF_TKN_COMP_PERIOD_SINK_COUNT", periodsSinkForNode(node, pipeline)},
    {"SOF_TKN_COMP_PERIOD_SOURCE_COUNT", periodsSourceForNode(node, pipeline)},
    {"SOF_TKN_COMP_CORE_ID", pipeline.scheduling.core},
  });
  emitTupleData(out, word_data, word_tuple);
  emitStringTuples(out, string_tuple, "sof_comp_tokens", {
    {"SOF_TKN_COMP_FORMAT", pcmFormatForSampleBits(pipeline.sample_bits)},
  });
  emitTupleData(out, string_data, string_tuple);

  std::vector<std::string> data_sections = {uuid_data, word_data, string_data};

  if (isDaiPortWidget(node, pipeline)) {
    const auto* port = findPort(pipeline, node);
    const uint32_t dai_index = port == nullptr ? pipeline.pcm_index : daiIndexForPort(*port, pipeline.pcm_index);
    const uint32_t dai_direction = pipeline.direction == "capture" ? 1 : 0;
    const std::string dai_word_tuple = base + "_dai_tuples_w";
    const std::string dai_word_data = base + "_dai_data_w";
    const std::string dai_string_tuple = base + "_dai_tuples_s";
    const std::string dai_string_data = base + "_dai_data_s";
    emitWordTuples(out, dai_word_tuple, "sof_dai_tokens", {
      {"SOF_TKN_DAI_INDEX", dai_index},
      {"SOF_TKN_DAI_DIRECTION", dai_direction},
    });
    emitTupleData(out, dai_word_data, dai_word_tuple);
    emitStringTuples(out, dai_string_tuple, "sof_dai_tokens", {
      {"SOF_TKN_DAI_TYPE", port == nullptr ? "VSI_TDM" : daiTypeForPort(*port)},
    });
    emitTupleData(out, dai_string_data, dai_string_tuple);
    data_sections.push_back(dai_word_data);
    data_sections.push_back(dai_string_data);
  }

  if (sofWidgetType(node, pipeline) == "effect") {
    const std::string process_tuple = base + "_process_tuples_s";
    const std::string process_data = base + "_process_data_s";
    emitStringTuples(out, process_tuple, "sof_process_tokens", {
      {"SOF_TKN_PROCESS_TYPE", processTypeForNode(node)},
    });
    emitTupleData(out, process_data, process_tuple);
    data_sections.push_back(process_data);
  }

  if (node.module_type == "rate.src") {
    const std::string src_tuple = base + "_src_tuples_w";
    const std::string src_data = base + "_src_data_w";
    emitWordTuples(out, src_tuple, "sof_src_tokens", {
      {"SOF_TKN_SRC_RATE_IN", pipeline.sample_rate},
      {"SOF_TKN_SRC_RATE_OUT", pipeline.sample_rate},
    });
    emitTupleData(out, src_data, src_tuple);
    data_sections.push_back(src_data);
  }

  if (node.module_type == "rate.asrc") {
    const std::string asrc_tuple = base + "_asrc_tuples_w";
    const std::string asrc_data = base + "_asrc_data_w";
    emitWordTuples(out, asrc_tuple, "sof_asrc_tokens", {
      {"SOF_TKN_ASRC_RATE_IN", pipeline.sample_rate},
      {"SOF_TKN_ASRC_RATE_OUT", pipeline.sample_rate},
      {"SOF_TKN_ASRC_ASYNCHRONOUS_MODE", 1},
      {"SOF_TKN_ASRC_OPERATION_MODE", 0},
    });
    emitTupleData(out, asrc_data, asrc_tuple);
    data_sections.push_back(asrc_data);
  }

  out << "SectionWidget." << topologyQuote(node.widget_name) << " {\n";
  out << "  index " << topologyQuote(std::to_string(pipeline.pcm_index + 1)) << "\n";
  out << "  type " << topologyQuote(sofWidgetType(node, pipeline)) << "\n";
  out << "  no_pm \"true\"\n";
  if (isHostPortWidget(node, pipeline) || isDaiPortWidget(node, pipeline)) {
    const auto* port = findPort(pipeline, node);
    std::string stream_name = pipeline.name;
    if (port != nullptr && isDaiPortWidget(node, pipeline) && !port->dai_id.empty()) {
      stream_name = port->dai_id;
    } else if (port != nullptr && !port->pcm_id.empty()) {
      stream_name = port->pcm_id;
    }
    out << "  stream_name " << topologyQuote(stream_name) << "\n";
  }
  out << "  data [\n";
  for (const auto& data : data_sections) out << "    " << topologyQuote(data) << "\n";
  out << "  ]\n";
  const auto control_names = controlsForNode(ir, pipeline, node);
  if (!control_names.mixer.empty()) {
    out << "  mixer [\n";
    for (const auto& control_name : control_names.mixer) out << "    " << topologyQuote(control_name) << "\n";
    out << "  ]\n";
  }
  if (!control_names.enumerated.empty()) {
    out << "  enum [\n";
    for (const auto& control_name : control_names.enumerated) out << "    " << topologyQuote(control_name) << "\n";
    out << "  ]\n";
  }
  if (!control_names.bytes.empty()) {
    out << "  bytes [\n";
    for (const auto& control_name : control_names.bytes) out << "    " << topologyQuote(control_name) << "\n";
    out << "  ]\n";
  }
  out << "}\n\n";
}

void emitDaiObjects(std::ostringstream& out, const PipelineInfo& pipeline) {
  uint32_t local_id = 0;
  for (const auto& node : pipeline.nodes) {
    if (!isDaiPortWidget(node, pipeline)) continue;
    const auto* port = findPort(pipeline, node);
    if (port == nullptr) continue;
    const std::string base = sofSectionBase(pipeline, node);
    const std::string dai_type = daiTypeForPort(*port);
    const uint32_t dai_index = daiIndexForPort(*port, local_id);
    const uint32_t link_id = pipeline.pcm_index * 16 + local_id;
    const std::string hw_name = base + "_hw";
    const std::string be_name = port->dai_id.empty() ? node.widget_name + ".BE" : port->dai_id;
    const std::string common_tuple = base + "_be_common_tuples";
    const std::string common_data = base + "_be_common_data";
    const std::string vsi_tuple = base + "_vsi_tdm_tuples";
    const std::string vsi_data = base + "_vsi_tdm_data";

    out << "SectionHWConfig." << topologyQuote(hw_name) << " {\n";
    out << "  id " << topologyQuote(std::to_string(link_id)) << "\n";
    out << "}\n\n";
    emitStringTuples(out, common_tuple + "_s", "sof_dai_tokens", {
      {"SOF_TKN_DAI_TYPE", dai_type},
    });
    emitTupleData(out, common_data + "_s", common_tuple + "_s");
    emitWordTuples(out, common_tuple + "_w", "sof_dai_tokens", {
      {"SOF_TKN_DAI_INDEX", dai_index},
    });
    emitTupleData(out, common_data + "_w", common_tuple + "_w");
    emitWordTuples(out, vsi_tuple, "sof_vsi_tdm_tokens", {
      {"SOF_TKN_VSI_TDM_MCLK_ID", 0},
      {"SOF_TKN_VSI_TDM_SAMPLE_WIDTH", port->sample_bits == 0 ? 32 : port->sample_bits},
      {"SOF_TKN_VSI_TDM_STEREO", port->max_ch == 2 ? 1 : 0},
      {"SOF_TKN_VSI_TDM_DELAY_MODE", 0},
      {"SOF_TKN_VSI_TDM_EDGE_MODE", 0},
      {"SOF_TKN_VSI_TDM_WS_POLARITY", 0},
    });
    emitTupleData(out, vsi_data, vsi_tuple);

    out << "SectionBE." << topologyQuote(be_name) << " {\n";
    out << "  id " << topologyQuote(std::to_string(link_id)) << "\n";
    out << "  index \"0\"\n";
    out << "  default_hw_conf_id " << topologyQuote(std::to_string(link_id)) << "\n";
    out << "  hw_configs [ " << topologyQuote(hw_name) << " ]\n";
    out << "  data [\n";
    out << "    " << topologyQuote(common_data + "_s") << "\n";
    out << "    " << topologyQuote(common_data + "_w") << "\n";
    if (dai_type == "VSI_TDM") out << "    " << topologyQuote(vsi_data) << "\n";
    out << "  ]\n";
    out << "}\n\n";

    out << "SectionDAI." << topologyQuote(be_name) << " {\n";
    out << "  index " << topologyQuote(std::to_string(pipeline.pcm_index + 1)) << "\n";
    out << "  id " << topologyQuote(std::to_string(link_id)) << "\n";
    out << "  playback " << topologyQuote(pipeline.direction == "playback" ? "1" : "0") << "\n";
    out << "  capture " << topologyQuote(pipeline.direction == "capture" ? "1" : "0") << "\n";
    out << "}\n\n";
    ++local_id;
  }
}

std::string generateAlsaConf(const ProjectIr& ir, const std::vector<uint8_t>& private_data) {
  (void)private_data;
  std::ostringstream out;
  out << "# Generated by Audio Studio as_config. Do not edit by hand.\n\n";
  emitSofVendorTokens(out);

  for (const auto& control : ir.controls) out << controlConf(control);

  for (const auto& pipeline : ir.pipelines) {
    emitPipelineScheduler(out, pipeline);
    for (const auto& node : pipeline.nodes) {
      emitSofWidgetData(out, ir, pipeline, node);
    }
    emitInferredBuffers(out, pipeline);
    emitDaiSchedulers(out, pipeline);
    emitDaiObjects(out, pipeline);
  }

  for (const auto& pipeline : ir.pipelines) {
    const std::string pcm_name = "A2 " + pipeline.name;
    const std::string caps_name = pcm_name + " Capabilities";
    out << "SectionPCMCapabilities." << topologyQuote(caps_name) << " {\n";
    out << "  formats " << topologyQuote(pcmFormatForSampleBits(pipeline.sample_bits)) << "\n";
    out << "  rate_min \"8000\"\n";
    out << "  rate_max " << topologyQuote(std::to_string(std::max<uint32_t>(pipeline.sample_rate, 192000))) << "\n";
    out << "  channels_min " << topologyQuote(std::to_string(pipeline.channels_min)) << "\n";
    out << "  channels_max " << topologyQuote(std::to_string(pipeline.channels_max)) << "\n";
    out << "  periods_min \"2\"\n";
    out << "  periods_max \"16\"\n";
    out << "  period_size_min \"192\"\n";
    out << "  period_size_max \"8192\"\n";
    out << "  buffer_size_min \"384\"\n";
    out << "  buffer_size_max \"65536\"\n";
    out << "}\n\n";
    out << "SectionPCM." << topologyQuote(pcm_name) << " {\n";
    out << "  index " << topologyQuote(std::to_string(pipeline.pcm_index + 1)) << "\n";
    out << "  id " << topologyQuote(std::to_string(pipeline.pcm_index)) << "\n";
    out << "  dai." << topologyQuote(pcm_name + " Pin") << " { id "
        << topologyQuote(std::to_string(pipeline.pcm_index)) << " }\n";
    out << "  pcm." << topologyQuote(pipeline.direction) << " { capabilities " << topologyQuote(caps_name) << " }\n";
    out << "}\n\n";
  }

  for (const auto& pipeline : ir.pipelines) {
    std::vector<std::string> main_lines;
    std::vector<std::pair<std::string, std::vector<std::string>>> dai_graphs;

    for (size_t edge_index = 0; edge_index < pipeline.edges.size(); ++edge_index) {
      const auto& edge = pipeline.edges[edge_index];
      const auto* from = findNode(pipeline, edge.from_node);
      const auto* to = findNode(pipeline, edge.to_node);
      if (from == nullptr || to == nullptr) continue;
      const std::string buffer_name = inferredBufferName(pipeline, edge_index);

      if (isDaiPortWidget(*from, pipeline)) {
        dai_graphs.push_back({from->widget_name + ".DAI", {buffer_name + ", , " + from->widget_name}});
        main_lines.push_back(to->widget_name + ", , " + buffer_name);
      } else if (isDaiPortWidget(*to, pipeline)) {
        main_lines.push_back(buffer_name + ", , " + from->widget_name);
        dai_graphs.push_back({to->widget_name + ".DAI", {to->widget_name + ", , " + buffer_name}});
      } else {
        main_lines.push_back(buffer_name + ", , " + from->widget_name);
        main_lines.push_back(to->widget_name + ", , " + buffer_name);
      }
    }

    out << "SectionGraph." << topologyQuote(pipeline.pipe_id) << " {\n";
    out << "  index " << topologyQuote(std::to_string(pipeline.pcm_index + 1)) << "\n";
    out << "  lines [\n";
    for (const auto& line : main_lines) {
      out << "    " << topologyQuote(line) << "\n";
    }
    out << "  ]\n";
    out << "}\n\n";

    for (const auto& dai_graph : dai_graphs) {
      out << "SectionGraph." << topologyQuote(dai_graph.first) << " {\n";
      out << "  index " << topologyQuote(std::to_string(pipeline.pcm_index + 1)) << "\n";
      out << "  lines [\n";
      for (const auto& line : dai_graph.second) {
        out << "    " << topologyQuote(line) << "\n";
      }
      out << "  ]\n";
      out << "}\n\n";
    }
  }
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
  out << "\"tplg_decoded\":" << (output.tplg_decoded ? "true" : "false") << ",";
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
  result["tplg_decoded"] = output.tplg_decoded;
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
    const auto already_loaded = std::any_of(libraries.begin(), libraries.end(),
                                            [&](const auto& library) { return library && library->path() == path; });
    if (already_loaded) continue;
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

std::string defaultBuiltinModuleConfigPluginPath(drivers::dynlib::IDynlibDriver* dynlib,
                                                 drivers::os::IOsDriver* os) {
  if (dynlib == nullptr) return {};

  const auto exe_dir = executableDirectory(os);
  if (exe_dir.empty()) return {};

  const auto relative_path =
      std::filesystem::path("plugins") / "builtin_module_configs" /
      ("libaudio_studio_builtin_module_configs" + dynlib->platformLibraryExtension());
  return findFileFromRoots({std::filesystem::path(exe_dir)}, relative_path);
}

Status loadDefaultBuiltinModuleConfigPlugin(
    drivers::dynlib::IDynlibDriver* dynlib,
    drivers::os::IOsDriver* os,
    ModuleConfigRegistry& registry,
    std::vector<std::unique_ptr<drivers::dynlib::IDynlib>>& libraries) {
  if (registry.findExact("filter.channel_remap") != nullptr) return Status::success();
  const auto builtin_plugin = defaultBuiltinModuleConfigPluginPath(dynlib, os);
  if (builtin_plugin.empty()) return Status::success();
  return loadPlugins(dynlib, {builtin_plugin}, registry, libraries);
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

  auto status = loadDefaultBuiltinModuleConfigPlugin(dynlib_, os_, module_configs_, plugin_libraries_);
  if (!status.ok()) return status;

  const size_t plugins_before = plugin_libraries_.size();
  status = loadPlugins(dynlib_, request.plugin_paths, module_configs_, plugin_libraries_);
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
    ir = parseProject(root, request.project_name, loadImportedModuleTypes(*filesystem_, root, request.input_path));
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
  output.module_instance_count = 0;
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

  const std::string alsa_conf = generateAlsaConf(ir, private_data);

  status = writeText(*filesystem_, output.conf_path, alsa_conf);
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
    const std::string alsatplg = resolveAlsaTplgExecutable(request.alsatplg, os_);
    const std::string command = shellQuote(alsatplg) + " -c " + shellQuote(output.conf_path) +
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
    output.tplg_built = true;

    const std::string decode_command = shellQuote(alsatplg) + " -d " + shellQuote(output.tplg_path) +
                                       " -o " + shellQuote(output.tplg_decode_conf_path) +
                                       " > " + shellQuote(output.tplg_decode_log_path) + " 2>&1";
    exit_code = -1;
    status = os_->process().runCommand(decode_command, exit_code);
    if (!status.ok()) return status;

    std::string decode_log;
    status = readText(*filesystem_, output.tplg_decode_log_path, decode_log);
    if (!status.ok()) return status;
    if (exit_code != 0 || containsAlsaTopologyError(decode_log)) {
      ir.warnings.push_back("alsatplg decode is not available for this topology; see " + output.tplg_decode_log_path);
    } else {
      output.tplg_decoded = true;
    }
  }

  output.ok = true;
  output.warnings = ir.warnings;
  status = writeText(*filesystem_, output.report_path, generateReport(request, output, ir, module_configs_.handlerIds()));
  if (!status.ok()) return status;

  (void)outputToJson(output);
  return Status::success();
}

} // namespace audio_studio::framework::config
