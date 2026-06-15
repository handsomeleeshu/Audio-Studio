#include "audio_studio/rpc/json_rpc.hpp"

#include <cctype>

namespace audio_studio::rpc {
namespace {

std::string escape(const std::string& input) {
  std::string out;
  out.reserve(input.size() + 8);
  for (const char c : input) {
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else out.push_back(c);
  }
  return out;
}

size_t skipSpace(const std::string& text, size_t pos) {
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
  return pos;
}

bool readJsonStringField(const std::string& json, const std::string& field, std::string& out) {
  const std::string key = "\"" + field + "\"";
  size_t pos = json.find(key);
  if (pos == std::string::npos) return false;
  pos = json.find(':', pos + key.size());
  if (pos == std::string::npos) return false;
  pos = skipSpace(json, pos + 1);
  if (pos >= json.size() || json[pos] != '"') return false;
  bool escaped = false;
  out.clear();
  for (size_t i = pos + 1; i < json.size(); ++i) {
    const char c = json[i];
    if (escaped) {
      out.push_back(c);
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else if (c == '"') {
      return true;
    } else {
      out.push_back(c);
    }
  }
  return false;
}

bool readRawJsonField(const std::string& json, const std::string& field, std::string& out) {
  const std::string key = "\"" + field + "\"";
  size_t pos = json.find(key);
  if (pos == std::string::npos) return false;
  pos = json.find(':', pos + key.size());
  if (pos == std::string::npos) return false;
  pos = skipSpace(json, pos + 1);
  if (pos >= json.size()) return false;
  size_t end = pos;
  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (; end < json.size(); ++end) {
    const char c = json[end];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (c == '\\' && in_string) {
      escaped = true;
      continue;
    }
    if (c == '"') {
      in_string = !in_string;
      continue;
    }
    if (in_string) continue;
    if (c == '{' || c == '[') ++depth;
    else if (c == '}' || c == ']') {
      if (depth == 0) break;
      --depth;
    } else if (c == ',' && depth == 0) {
      break;
    }
  }
  out = json.substr(pos, end - pos);
  return !out.empty();
}

} // namespace

framework::Status parseRequest(const std::string& json, JsonRpcRequest& out) {
  if (!readJsonStringField(json, "method", out.method)) return framework::Status::invalidArgument("json-rpc method is missing");
  if (!readJsonStringField(json, "id", out.id)) out.id = "null";
  if (!readRawJsonField(json, "params", out.params_json)) out.params_json = "{}";
  return framework::Status::success();
}

std::string resultResponse(const std::string& id, const std::string& result_json) {
  return "{\"jsonrpc\":\"2.0\",\"id\":\"" + escape(id) + "\",\"result\":" + result_json + "}";
}

std::string errorResponse(const std::string& id, int code, const std::string& message) {
  return "{\"jsonrpc\":\"2.0\",\"id\":\"" + escape(id) + "\",\"error\":{\"code\":" + std::to_string(code) + ",\"message\":\"" + escape(message) + "\"}}";
}

} // namespace audio_studio::rpc
