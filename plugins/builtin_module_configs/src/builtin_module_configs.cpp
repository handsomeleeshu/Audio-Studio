#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "json_value.hpp"
#include "module_config_plugin.hpp"

namespace {

namespace module_config = audio_studio::module_config;
using audio_studio::rpc::JsonValue;

std::string macroToken(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  bool last_was_sep = false;
  for (const unsigned char raw : value) {
    if (std::isalnum(raw)) {
      out.push_back(static_cast<char>(std::toupper(raw)));
      last_was_sep = false;
    } else if (!last_was_sep && !out.empty()) {
      out.push_back('_');
      last_was_sep = true;
    }
  }
  while (!out.empty() && out.back() == '_') out.pop_back();
  if (out.empty()) out = "ITEM";
  return out;
}

std::string lowerAscii(std::string value) {
  for (auto& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return value;
}

std::string stringValue(const JsonValue& object, const std::string& key, const std::string& fallback = {}) {
  if (!object.isObject() || !object.has(key) || object.at(key).isNull()) return fallback;
  if (!object.at(key).isString()) throw std::runtime_error("JSON field must be string: " + key);
  return object.at(key).asString();
}

uint32_t uintValue(const JsonValue& object, const std::string& key, uint32_t fallback) {
  if (!object.isObject() || !object.has(key) || object.at(key).isNull()) return fallback;
  if (!object.at(key).isNumber()) throw std::runtime_error("JSON field must be number: " + key);
  const auto value = object.at(key).asInt64();
  if (value < 0) throw std::runtime_error("JSON field must be non-negative: " + key);
  return static_cast<uint32_t>(value);
}

const JsonValue& requiredArray(const JsonValue& object, const std::string& key) {
  if (!object.isObject() || !object.has(key) || !object.at(key).isArray()) {
    throw std::runtime_error("JSON field must be array: " + key);
  }
  return object.at(key);
}

void appendLe16(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
}

void appendLe32(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
}

std::string makeSlug(std::string module_type) {
  for (auto& ch : module_type) {
    if (ch == '.' || ch == '_') ch = '-';
  }
  return module_type;
}

class SofBasicModuleConfigHandler final : public module_config::IModuleConfigHandler {
public:
  explicit SofBasicModuleConfigHandler(std::string module_type) : module_type_(std::move(module_type)) {}

  std::string id() const override { return "as.sof-basic-module-config-v1." + macroToken(module_type_); }
  std::string moduleType() const override { return module_type_; }

  module_config::Status validatePreset(const module_config::ModuleConfigRequest& request) const override {
    if (request.module_type != module_type_) {
      return module_config::Status::invalidArgument("unexpected module_type for SOF basic handler: " + request.module_type);
    }
    if (request.values_json.empty()) return module_config::Status::invalidArgument("module config request values_json is empty");
    return module_config::Status::success();
  }

  module_config::Status packPreset(const module_config::ModuleConfigRequest& request,
                                   module_config::ModuleConfigBlob& out) const override {
    auto status = validatePreset(request);
    if (!status.ok()) return status;
    out.format = "as-sof-basic-preset-json-v1";
    out.data.assign(request.values_json.begin(), request.values_json.end());
    return module_config::Status::success();
  }

  module_config::Status packInstallConfig(const module_config::ModuleConfigRequest& request,
                                          module_config::ModuleConfigBlob& out) const override {
    return packRuntimeParam(request, out);
  }

  module_config::Status packRuntimeParam(const module_config::ModuleConfigRequest& request,
                                         module_config::ModuleConfigBlob& out) const override {
    try {
      auto status = validatePreset(request);
      if (!status.ok()) return status;
      const auto values = audio_studio::rpc::parseJson(request.values_json);
      if (!values.isObject() || !values.has(request.parameter_id)) {
        return module_config::Status::invalidArgument("parameter value missing: " + request.parameter_id);
      }

      const auto& value = values.at(request.parameter_id);
      std::vector<uint8_t> payload;
      if (module_type_ == "filter.channel_remap" || module_type_ == "filter.chremap") {
        payload = packChannelRemap(request.parameter_id == "config" ? value : channelRemapPreset(value));
      } else if (module_type_ == "delay.line" || module_type_ == "delay.delay_line") {
        payload = packDelayLine(request.parameter_id == "config" ? value : delayLineParam(request.parameter_id, value));
      } else if (module_type_ == "mix.fader_balance") {
        payload = packFaderBalance(request.parameter_id == "config" ? value : faderBalanceParam(request.parameter_id, value));
      } else if (module_type_ == "filter.dsp_filter") {
        payload = packDspFilter(request.parameter_id == "config" ? value : dspFilterPreset(value));
      } else {
        return module_config::Status::unavailable("unsupported SOF basic module_type: " + module_type_);
      }

      out.format = "sof-ipc3-bytes-v1";
      out.data = wrapSofAbi(payload);
      return module_config::Status::success();
    } catch (const std::exception& error) {
      return module_config::Status::invalidArgument(
          "failed to pack " + module_type_ + " parameter " + request.parameter_id + ": " + error.what());
    }
  }

private:
  std::string module_type_;

  static void align4(std::vector<uint8_t>& out) {
    while ((out.size() & 3u) != 0) out.push_back(0);
  }

  static void appendLe32Signed(std::vector<uint8_t>& out, int32_t value) {
    appendLe32(out, static_cast<uint32_t>(value));
  }

  static std::vector<uint8_t> wrapSofAbi(const std::vector<uint8_t>& data) {
    constexpr uint32_t kSofAbiMagic = 0x00464f53u;
    constexpr uint32_t kSofAbiVersion = 0x0301d001u;
    std::vector<uint8_t> out;
    appendLe32(out, kSofAbiMagic);
    appendLe32(out, 0);
    appendLe32(out, static_cast<uint32_t>(data.size()));
    appendLe32(out, kSofAbiVersion);
    appendLe32(out, 0);
    appendLe32(out, 0);
    appendLe32(out, 0);
    appendLe32(out, 0);
    out.insert(out.end(), data.begin(), data.end());
    return out;
  }

  static std::vector<uint16_t> channelList(const JsonValue& object, const std::string& key) {
    std::vector<uint16_t> channels;
    for (const auto& item : requiredArray(object, key).asArray()) {
      if (!item.isString()) throw std::runtime_error(key + " entries must be channel names");
      channels.push_back(channelMap(item.asString()));
    }
    return channels;
  }

  static uint16_t channelMap(const std::string& name) {
    const auto token = macroToken(name);
    static const std::map<std::string, uint16_t> values = {
      {"UNKNOWN", 0}, {"NA", 1}, {"MONO", 2}, {"FL", 3}, {"FR", 4}, {"RL", 5}, {"RR", 6},
      {"FC", 7}, {"LFE", 8}, {"SL", 9}, {"SR", 10}, {"RC", 11}, {"FLC", 12}, {"FRC", 13},
      {"RLC", 14}, {"RRC", 15}, {"FLW", 16}, {"FRW", 17}, {"FLH", 18}, {"FCH", 19},
      {"FRH", 20}, {"TC", 21}, {"TFL", 22}, {"TFR", 23}, {"TFC", 24}, {"TRL", 25},
      {"TRR", 26}, {"TRC", 27}, {"TFLC", 28}, {"TFRC", 29}, {"TSL", 30}, {"TSR", 31},
      {"LLFE", 32}, {"RLFE", 33}, {"BC", 34}, {"BLC", 35}, {"BRC", 36},
    };
    const auto it = values.find(token);
    if (it == values.end()) throw std::runtime_error("unknown channel name: " + name);
    return it->second;
  }

  static int32_t q5_27(double value) {
    constexpr double scale = 134217728.0;
    value = std::max(-16.0, std::min(15.999999, value));
    return static_cast<int32_t>(std::llround(value * scale));
  }

  static int16_t q1_15(double value) {
    value = std::max(-1.0, std::min(1.0, value));
    if (value >= 1.0) return 32767;
    return static_cast<int16_t>(std::llround(value * 32768.0));
  }

  static int32_t scaledWeight(double value, int32_t scale) {
    value = std::max(-1.0, std::min(1.0, value));
    return static_cast<int32_t>(std::llround(value * scale));
  }

  static std::string jsonNumber(double value) {
    std::ostringstream out;
    out << std::setprecision(12) << value;
    return out.str();
  }

  static std::string scalarString(const JsonValue& value, const std::string& fallback) {
    if (value.isString()) return lowerAscii(value.asString());
    if (value.isNumber()) return std::to_string(value.asInt64());
    return fallback;
  }

  static double scalarDouble(const JsonValue& value, double fallback) {
    if (value.isNumber()) return value.asDouble();
    if (value.isString()) {
      try {
        return std::stod(value.asString());
      } catch (const std::exception&) {
        return fallback;
      }
    }
    return fallback;
  }

  static JsonValue channelRemapPreset(const JsonValue& value) {
    const auto layout = scalarString(value, "stereo_passthrough");
    if (layout == "mono_passthrough") {
      return audio_studio::rpc::parseJson(R"({"mappings":[{"name":"mono_passthrough","config_idx":0,"input":["MONO"],"output":["MONO"],"matrix":[[1]]}]})");
    }
    if (layout == "mono_to_stereo") {
      return audio_studio::rpc::parseJson(R"({"mappings":[{"name":"mono_to_stereo","config_idx":0,"input":["MONO"],"output":["FL","FR"],"matrix":[[1],[1]]}]})");
    }
    if (layout == "stereo_to_mono") {
      return audio_studio::rpc::parseJson(R"({"mappings":[{"name":"stereo_to_mono","config_idx":0,"input":["FL","FR"],"output":["MONO"],"matrix":[[0.5,0.5]]}]})");
    }
    return audio_studio::rpc::parseJson(R"({"mappings":[{"name":"stereo_passthrough","config_idx":0,"input":["FL","FR"],"output":["FL","FR"],"matrix":[[1,0],[0,1]]}]})");
  }

  static JsonValue delayLineParam(const std::string& parameter_id, const JsonValue& value) {
    const double delay_ms = parameter_id == "delay_ms" ? scalarDouble(value, 0.0) : 0.0;
    const double max_delay_ms = parameter_id == "max_delay_ms" ? scalarDouble(value, 250.0) : 250.0;
    const double ramp_ms = parameter_id == "ramp_ms" ? scalarDouble(value, 50.0) : 50.0;
    const auto json = std::string("{\"max_delay_ms\":") + jsonNumber(max_delay_ms) +
        ",\"ramp_ms\":" + jsonNumber(ramp_ms) +
        ",\"channels\":2,\"per_channel_ms\":[" + jsonNumber(delay_ms) + "," + jsonNumber(delay_ms) + "]}";
    return audio_studio::rpc::parseJson(json);
  }

  static JsonValue faderBalanceParam(const std::string& parameter_id, const JsonValue& value) {
    const double fader = parameter_id == "fader" ? scalarDouble(value, 0.0) : 0.0;
    const double balance = parameter_id == "balance" ? scalarDouble(value, 0.0) : 0.0;
    const double ramp_ms = parameter_id == "ramp_ms" ? scalarDouble(value, 50.0) : 50.0;
    const auto json = std::string("{\"front_back_weight\":") + jsonNumber(fader) +
        ",\"left_right_weight\":" + jsonNumber(balance) +
        ",\"ramp\":\"linear\",\"ramp_ms\":" + jsonNumber(ramp_ms) + "}";
    return audio_studio::rpc::parseJson(json);
  }

  static JsonValue dspFilterPreset(const JsonValue& value) {
    const auto preset = scalarString(value, "passthrough");
    if (preset == "lowpass") {
      return audio_studio::rpc::parseJson(R"({"filters":[{"filter_id":"lowpass_fir","index":0,"type":"fir","coefficients":[0.25,0.5,0.25]}],"routes":[{"name":"lowpass_stereo","config_idx":0,"input":["FL","FR"],"output":["FL","FR"],"matrix":[["lowpass_fir",null],[null,"lowpass_fir"]]}]})");
    }
    if (preset == "highpass") {
      return audio_studio::rpc::parseJson(R"({"filters":[{"filter_id":"highpass_fir","index":0,"type":"fir","coefficients":[-0.25,0.5,-0.25]}],"routes":[{"name":"highpass_stereo","config_idx":0,"input":["FL","FR"],"output":["FL","FR"],"matrix":[["highpass_fir",null],[null,"highpass_fir"]]}]})");
    }
    return audio_studio::rpc::parseJson(R"({"filters":[{"filter_id":"identity_fir","index":0,"type":"fir","coefficients":[1]}],"routes":[{"name":"stereo_passthrough","config_idx":0,"input":["FL","FR"],"output":["FL","FR"],"matrix":[["identity_fir",null],[null,"identity_fir"]]}]})");
  }

  static double doubleAny(const JsonValue& object,
                          const std::vector<std::string>& keys,
                          double fallback) {
    for (const auto& key : keys) {
      if (object.isObject() && object.has(key) && !object.at(key).isNull()) {
        if (!object.at(key).isNumber()) throw std::runtime_error("JSON field must be number: " + key);
        return object.at(key).asDouble();
      }
    }
    return fallback;
  }

  static int32_t int32Any(const JsonValue& object,
                          const std::vector<std::string>& keys,
                          int32_t fallback) {
    for (const auto& key : keys) {
      if (object.isObject() && object.has(key) && !object.at(key).isNull()) {
        if (!object.at(key).isNumber()) throw std::runtime_error("JSON field must be number: " + key);
        return static_cast<int32_t>(object.at(key).asInt64());
      }
    }
    return fallback;
  }

  static std::vector<uint8_t> packChannelRemapConfig(const JsonValue& mapping, uint32_t fallback_idx) {
    const auto inputs = channelList(mapping, "input");
    const auto outputs = channelList(mapping, "output");
    const auto& matrix = requiredArray(mapping, "matrix");
    if (matrix.asArray().size() != outputs.size()) throw std::runtime_error("channel_remap matrix row count must match output channels");

    std::vector<uint8_t> out;
    appendLe32(out, uintValue(mapping, "config_idx", fallback_idx));
    appendLe32(out, static_cast<uint32_t>(inputs.size()));
    appendLe32(out, static_cast<uint32_t>(outputs.size()));
    for (int i = 0; i < 5; ++i) appendLe32(out, 0);
    for (const auto channel : inputs) appendLe16(out, channel);
    for (const auto channel : outputs) appendLe16(out, channel);
    align4(out);
    for (size_t row = 0; row < outputs.size(); ++row) {
      const auto& row_value = matrix.asArray()[row];
      if (!row_value.isArray() || row_value.asArray().size() != inputs.size()) {
        throw std::runtime_error("channel_remap matrix column count must match input channels");
      }
      for (const auto& coef : row_value.asArray()) {
        if (!coef.isNumber()) throw std::runtime_error("channel_remap matrix values must be numbers");
        appendLe32Signed(out, q5_27(coef.asDouble()));
      }
    }
    return out;
  }

  static std::vector<uint8_t> packChannelRemap(const JsonValue& value) {
    const auto& mappings = requiredArray(value, "mappings");
    std::vector<uint8_t> payload;
    for (size_t i = 0; i < mappings.asArray().size(); ++i) {
      auto config = packChannelRemapConfig(mappings.asArray()[i], static_cast<uint32_t>(i));
      payload.insert(payload.end(), config.begin(), config.end());
    }
    return payload;
  }

  static std::vector<int32_t> delayValues(const JsonValue& value, int32_t channels) {
    std::vector<int32_t> delays;
    if (value.has("per_channel_ms")) {
      const auto& items = requiredArray(value, "per_channel_ms").asArray();
      for (const auto& item : items) {
        if (!item.isNumber()) throw std::runtime_error("per_channel_ms entries must be numbers");
        delays.push_back(static_cast<int32_t>(std::llround(item.asDouble())));
      }
    } else {
      const int32_t delay = int32Any(value, {"delay_ms"}, 0);
      delays.assign(static_cast<size_t>(std::max<int32_t>(1, channels)), delay);
    }
    if (channels == 0) channels = static_cast<int32_t>(delays.size());
    if (static_cast<int32_t>(delays.size()) != channels) throw std::runtime_error("delay_line channel count does not match delay values");
    return delays;
  }

  static std::vector<uint8_t> packDelayLine(const JsonValue& value) {
    int32_t channels = int32Any(value, {"channels"}, 0);
    auto delays = delayValues(value, channels);
    channels = static_cast<int32_t>(delays.size());
    std::vector<uint8_t> payload;
    appendLe32Signed(payload, int32Any(value, {"max_delay_ms"}, 250));
    appendLe32Signed(payload, int32Any(value, {"ramp_ms", "ramp_time_ms"}, 50));
    appendLe32Signed(payload, channels);
    for (const auto delay : delays) appendLe32Signed(payload, delay);
    return payload;
  }

  static std::vector<uint8_t> packFaderBalance(const JsonValue& value) {
    const std::string ramp = lowerAscii(stringValue(value, "ramp", "linear"));
    const uint32_t ramp_type = ramp == "windows_fade" || ramp == "windows" ? 1u : 0u;
    std::vector<uint8_t> payload;
    appendLe32(payload, ramp_type);
    appendLe32Signed(payload, int32Any(value, {"ramp_ms", "ramp_time_ms"}, 50));
    appendLe32Signed(payload, scaledWeight(doubleAny(value, {"front_back_weight", "fader_weight"}, 0.0), 256));
    appendLe32Signed(payload, scaledWeight(doubleAny(value, {"left_right_weight", "balance_weight"}, 0.0), 256));
    return payload;
  }

  static std::vector<uint8_t> packDspFilterHeader(const JsonValue& filter, uint16_t fallback_idx) {
    const std::string type = lowerAscii(stringValue(filter, "type", "fir"));
    const uint16_t idx = static_cast<uint16_t>(uintValue(filter, "index", fallback_idx));
    std::vector<int16_t> coefficients;
    uint16_t filter_type = 0;
    if (type == "fir") {
      filter_type = 1;
      const auto& items = requiredArray(filter, "coefficients").asArray();
      for (const auto& item : items) {
        if (!item.isNumber()) throw std::runtime_error("dsp_filter coefficients must be numbers");
        coefficients.push_back(q1_15(item.asDouble()));
      }
      if (coefficients.empty()) throw std::runtime_error("dsp_filter FIR must have at least one coefficient");
    } else if (type == "bypass") {
      filter_type = 0;
    } else {
      throw std::runtime_error("unsupported dsp_filter type: " + type);
    }

    std::vector<uint8_t> out;
    appendLe16(out, idx);
    appendLe16(out, filter_type);
    appendLe16(out, 0);
    appendLe16(out, static_cast<uint16_t>(coefficients.size()));
    appendLe16(out, 0);
    appendLe16(out, static_cast<uint16_t>(coefficients.size() * sizeof(int16_t)));
    for (const auto coef : coefficients) appendLe16(out, static_cast<uint16_t>(coef));
    align4(out);
    return out;
  }

  static int16_t filterMatrixEntry(const JsonValue& entry, const std::map<std::string, int16_t>& filter_ids) {
    if (entry.isNull()) return -1;
    if (entry.isNumber()) return static_cast<int16_t>(entry.asInt64());
    if (entry.isString()) {
      const auto text = entry.asString();
      if (text.empty() || text == "off" || text == "none" || text == "null") return -1;
      const auto it = filter_ids.find(text);
      if (it == filter_ids.end()) throw std::runtime_error("dsp_filter matrix references unknown filter: " + text);
      return it->second;
    }
    if (entry.isObject()) {
      return filterMatrixEntry(entry.has("filter") ? entry.at("filter") : entry.at("filter_id"), filter_ids);
    }
    throw std::runtime_error("dsp_filter matrix entry must be filter id, number, or null");
  }

  static std::vector<uint8_t> packDspFilterChannelConfig(const JsonValue& route,
                                                         uint32_t fallback_idx,
                                                         const std::map<std::string, int16_t>& filter_ids) {
    const auto inputs = channelList(route, "input");
    const auto outputs = channelList(route, "output");
    const auto& matrix = requiredArray(route, "matrix");
    if (matrix.asArray().size() != outputs.size()) throw std::runtime_error("dsp_filter matrix row count must match output channels");

    std::vector<uint8_t> data;
    for (const auto channel : inputs) appendLe16(data, channel);
    for (const auto channel : outputs) appendLe16(data, channel);
    for (size_t row = 0; row < outputs.size(); ++row) {
      const auto& row_value = matrix.asArray()[row];
      if (!row_value.isArray() || row_value.asArray().size() != inputs.size()) {
        throw std::runtime_error("dsp_filter matrix column count must match input channels");
      }
      for (const auto& entry : row_value.asArray()) {
        appendLe16(data, static_cast<uint16_t>(filterMatrixEntry(entry, filter_ids)));
      }
    }
    align4(data);

    std::vector<uint8_t> out;
    appendLe32(out, uintValue(route, "config_idx", fallback_idx));
    appendLe32(out, static_cast<uint32_t>(inputs.size()));
    appendLe32(out, static_cast<uint32_t>(outputs.size()));
    appendLe32(out, static_cast<uint32_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
    align4(out);
    return out;
  }

  static std::vector<uint8_t> packDspFilter(const JsonValue& value) {
    const auto& filters = requiredArray(value, "filters");
    const auto& routes = requiredArray(value, "routes");
    std::vector<uint8_t> data;
    std::map<std::string, int16_t> filter_ids;
    for (size_t i = 0; i < filters.asArray().size(); ++i) {
      const auto& filter = filters.asArray()[i];
      const uint16_t index = static_cast<uint16_t>(uintValue(filter, "index", static_cast<uint32_t>(i)));
      const std::string filter_id = stringValue(filter, "filter_id", stringValue(filter, "id"));
      if (!filter_id.empty()) filter_ids[filter_id] = static_cast<int16_t>(index);
      auto packed = packDspFilterHeader(filter, static_cast<uint16_t>(i));
      data.insert(data.end(), packed.begin(), packed.end());
    }
    for (size_t i = 0; i < routes.asArray().size(); ++i) {
      auto packed = packDspFilterChannelConfig(routes.asArray()[i], static_cast<uint32_t>(i), filter_ids);
      data.insert(data.end(), packed.begin(), packed.end());
    }

    std::vector<uint8_t> payload;
    appendLe32(payload, static_cast<uint32_t>(filters.asArray().size()));
    appendLe32(payload, static_cast<uint32_t>(routes.asArray().size()));
    appendLe32(payload, static_cast<uint32_t>(data.size()));
    payload.insert(payload.end(), data.begin(), data.end());
    return payload;
  }
};

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

const std::vector<std::string>& sofBasicModuleTypes() {
  static const std::vector<std::string> module_types = {
    "filter.channel_remap",
    "delay.line",
    "mix.fader_balance",
    "filter.dsp_filter",
  };
  return module_types;
}

} // namespace

extern "C" bool audio_studio_register_module_config_handlers_v1(
    audio_studio::module_config::IModuleConfigRegistry& registry) {
  for (const auto& module_type : sofBasicModuleTypes()) {
    const auto status = registry.registerHandler(std::make_unique<SofBasicModuleConfigHandler>(module_type));
    if (!status.ok()) return false;
  }
  for (const auto& module_type : builtinModuleTypes()) {
    const auto status = registry.registerHandler(std::make_unique<BuiltinJsonModuleConfigHandler>(module_type));
    if (!status.ok()) return false;
  }
  return true;
}
