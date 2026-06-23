#include "audio_studio.hpp"

#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace audiostudio {
namespace {

namespace fs = std::filesystem;

std::string jsonMemberValue(const std::string& object_json, const std::string& field);

std::string readTextFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool writeTextFile(const std::string& path, const std::string& text) {
  fs::create_directories(fs::path(path).parent_path());
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out << text;
  return static_cast<bool>(out);
}

std::string simpleJsonStringField(const std::string& body, const std::string& field, const std::string& fallback = "") {
  const std::string key = std::string("\"") + field + "\"";
  size_t pos = body.find(key);
  if (pos == std::string::npos) return fallback;
  pos = body.find(':', pos + key.size());
  if (pos == std::string::npos) return fallback;
  pos = body.find('"', pos + 1);
  if (pos == std::string::npos) return fallback;
  std::string out;
  bool esc = false;
  for (size_t i = pos + 1; i < body.size(); ++i) {
    const char c = body[i];
    if (esc) {
      out.push_back(c);
      esc = false;
      continue;
    }
    if (c == '\\') {
      esc = true;
      continue;
    }
    if (c == '"') return out.empty() ? fallback : out;
    out.push_back(c);
  }
  return fallback;
}

std::vector<std::string> simpleJsonStringArrayField(const std::string& body, const std::string& field) {
  std::vector<std::string> out;
  const std::string array = jsonMemberValue(body, field);
  if (array.empty() || array.front() != '[') return out;
  bool in_string = false;
  bool esc = false;
  std::string current;
  for (size_t i = 1; i < array.size(); ++i) {
    const char c = array[i];
    if (!in_string) {
      if (c == '"') {
        in_string = true;
        current.clear();
      } else if (c == ']') {
        break;
      }
      continue;
    }
    if (esc) {
      current.push_back(c);
      esc = false;
      continue;
    }
    if (c == '\\') {
      esc = true;
      continue;
    }
    if (c == '"') {
      in_string = false;
      out.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(c);
  }
  return out;
}

std::string safeId(const std::string& value) {
  std::string out;
  for (char c : value) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9');
    out.push_back(ok ? c : '_');
  }
  while (!out.empty() && out.back() == '_') out.pop_back();
  return out.empty() ? "project" : out;
}

std::string projectNameFromProjectPath(const std::string& project) {
  const auto slash = project.find('/');
  if (slash == std::string::npos) {
    const auto dot = project.rfind('.');
    return safeId(dot == std::string::npos ? project : project.substr(0, dot));
  }
  return safeId(project.substr(0, slash));
}

size_t skipWhitespaceBack(const std::string& text, size_t pos) {
  while (pos > 0) {
    const char c = text[pos - 1];
    if (c != ' ' && c != '\n' && c != '\r' && c != '\t') break;
    --pos;
  }
  return pos;
}

size_t matchingJsonValueEnd(const std::string& text, size_t value_start) {
  if (value_start >= text.size()) return std::string::npos;
  const char first = text[value_start];
  if (first == '{' || first == '[') {
    int depth = 0;
    bool in_string = false;
    bool esc = false;
    for (size_t i = value_start; i < text.size(); ++i) {
      const char c = text[i];
      if (in_string) {
        if (esc) esc = false;
        else if (c == '\\') esc = true;
        else if (c == '"') in_string = false;
        continue;
      }
      if (c == '"') in_string = true;
      else if (c == first) ++depth;
      else if ((first == '{' && c == '}') || (first == '[' && c == ']')) {
        --depth;
        if (depth == 0) return i + 1;
      }
    }
    return std::string::npos;
  }
  if (first == '"') {
    bool esc = false;
    for (size_t i = value_start + 1; i < text.size(); ++i) {
      const char c = text[i];
      if (esc) esc = false;
      else if (c == '\\') esc = true;
      else if (c == '"') return i + 1;
    }
    return std::string::npos;
  }
  size_t end = value_start;
  while (end < text.size() && text[end] != ',' && text[end] != '}' && text[end] != ']') ++end;
  return end;
}

std::string jsonMemberValue(const std::string& object_json, const std::string& field) {
  const std::string key = std::string("\"") + field + "\"";
  size_t pos = object_json.find(key);
  if (pos == std::string::npos) return {};
  pos = object_json.find(':', pos + key.size());
  if (pos == std::string::npos) return {};
  ++pos;
  while (pos < object_json.size() &&
         (object_json[pos] == ' ' || object_json[pos] == '\n' || object_json[pos] == '\r' || object_json[pos] == '\t')) {
    ++pos;
  }
  const size_t end = matchingJsonValueEnd(object_json, pos);
  if (end == std::string::npos || end <= pos) return {};
  return object_json.substr(pos, end - pos);
}

std::string appendObjectFields(const std::string& json, const std::string& fields_without_leading_comma) {
  const size_t end = skipWhitespaceBack(json, json.size());
  if (end == 0 || json[end - 1] != '}') return json;
  std::string out = json.substr(0, end - 1);
  const size_t before_close = skipWhitespaceBack(out, out.size());
  const bool has_existing_fields = before_close > 0 && out.find('{') != std::string::npos && out[before_close - 1] != '{';
  out += has_existing_fields ? "," : "";
  out += fields_without_leading_comma;
  out += json.substr(end - 1);
  return out;
}

bool topLevelMemberRange(const std::string& object_json, const std::string& field, size_t& member_begin, size_t& member_end) {
  if (object_json.empty() || object_json.front() != '{') return false;
  const std::string key = std::string("\"") + field + "\"";
  bool in_string = false;
  bool esc = false;
  int depth = 0;
  for (size_t i = 0; i < object_json.size(); ++i) {
    const char c = object_json[i];
    if (in_string) {
      if (esc) esc = false;
      else if (c == '\\') esc = true;
      else if (c == '"') in_string = false;
      continue;
    }
    if (c == '"') {
      if (depth == 1 && object_json.compare(i, key.size(), key) == 0) {
        size_t colon = object_json.find(':', i + key.size());
        if (colon == std::string::npos) return false;
        size_t value_start = colon + 1;
        while (value_start < object_json.size() &&
               (object_json[value_start] == ' ' || object_json[value_start] == '\n' ||
                object_json[value_start] == '\r' || object_json[value_start] == '\t')) {
          ++value_start;
        }
        size_t value_end = matchingJsonValueEnd(object_json, value_start);
        if (value_end == std::string::npos) return false;
        member_begin = i;
        member_end = value_end;
        size_t before = skipWhitespaceBack(object_json, member_begin);
        if (before > 0 && object_json[before - 1] == ',') member_begin = before - 1;
        else {
          size_t after = member_end;
          while (after < object_json.size() &&
                 (object_json[after] == ' ' || object_json[after] == '\n' ||
                  object_json[after] == '\r' || object_json[after] == '\t')) {
            ++after;
          }
          if (after < object_json.size() && object_json[after] == ',') member_end = after + 1;
        }
        return true;
      }
      in_string = true;
    } else if (c == '{' || c == '[') {
      ++depth;
    } else if (c == '}' || c == ']') {
      --depth;
    }
  }
  return false;
}

std::string upsertObjectMember(const std::string& object_json, const std::string& field, const std::string& value_json) {
  size_t begin = 0;
  size_t end = 0;
  if (topLevelMemberRange(object_json, field, begin, end)) {
    std::string out = object_json;
    const std::string replacement = "\"" + field + "\":" + value_json;
    const bool need_leading_comma = begin > 0 && object_json[begin] == ',';
    out.replace(begin, end - begin, need_leading_comma ? "," + replacement : replacement);
    return out;
  }
  return appendObjectFields(object_json, "\"" + field + "\":" + value_json);
}

std::vector<std::pair<std::string, std::string>> pipelineDescriptionsFromSnapshot(const std::string& snapshot_json) {
  std::vector<std::pair<std::string, std::string>> out;
  const std::string array = jsonMemberValue(snapshot_json, "pipeline_descriptions");
  if (array.empty() || array.front() != '[') return out;
  for (size_t i = 1; i < array.size();) {
    while (i < array.size() && array[i] != '{' && array[i] != ']') ++i;
    if (i >= array.size() || array[i] == ']') break;
    const size_t object_end = matchingJsonValueEnd(array, i);
    if (object_end == std::string::npos) break;
    const std::string object = array.substr(i, object_end - i);
    const std::string pipe_id = simpleJsonStringField(object, "pipe_id");
    const std::string name = simpleJsonStringField(object, "name");
    if (!pipe_id.empty() && !name.empty()) out.emplace_back(pipe_id, name);
    i = object_end;
  }
  return out;
}

std::string updatePipelineNamesFromSnapshot(std::string json, const std::string& snapshot_json) {
  const auto descriptions = pipelineDescriptionsFromSnapshot(snapshot_json);
  if (descriptions.empty()) return json;
  for (const auto& [pipe_id, name] : descriptions) {
    size_t search = 0;
    while (true) {
      const size_t id_pos = json.find("\"pipe_id\"", search);
      if (id_pos == std::string::npos) break;
      const size_t object_begin = json.rfind('{', id_pos);
      if (object_begin == std::string::npos) break;
      const size_t object_end = matchingJsonValueEnd(json, object_begin);
      if (object_end == std::string::npos || object_end <= object_begin) break;
      const std::string object = json.substr(object_begin, object_end - object_begin);
      if (simpleJsonStringField(object, "pipe_id") == pipe_id) {
        const std::string updated = upsertObjectMember(object, "name", "\"" + jsonEscape(name) + "\"");
        json.replace(object_begin, object_end - object_begin, updated);
        search = object_begin + updated.size();
        break;
      }
      search = object_end;
    }
  }
  return json;
}

std::string jsonStringArray(const std::vector<std::string>& values) {
  std::ostringstream os;
  os << "[";
  for (size_t i = 0; i < values.size(); ++i) {
    if (i) os << ",";
    os << "\"" << jsonEscape(values[i]) << "\"";
  }
  os << "]";
  return os.str();
}

std::string shellQuote(const std::string& value) {
  std::string out = "'";
  for (char c : value) {
    if (c == '\'') out += "'\\''";
    else out.push_back(c);
  }
  out += "'";
  return out;
}

std::string envString(const char* name, const std::string& fallback) {
  const char* value = std::getenv(name);
  return value && *value ? std::string(value) : fallback;
}

uint16_t envPort(const char* name, uint16_t fallback) {
  const char* value = std::getenv(name);
  if (!value || !*value) return fallback;
  char* end = nullptr;
  const long parsed = std::strtol(value, &end, 10);
  if (!end || *end != '\0' || parsed <= 0 || parsed > 65535) return fallback;
  return static_cast<uint16_t>(parsed);
}

std::string resolveValidationScriptPath(const std::string& root_dir) {
  const fs::path root(root_dir);
  const std::vector<fs::path> candidates = {
      root / "application/rv32qemu/sof-build-test.py",
      root.parent_path() / "application/rv32qemu/sof-build-test.py",
      fs::current_path() / "application/rv32qemu/sof-build-test.py"};
  for (const auto& candidate : candidates) {
    if (fs::exists(candidate)) return candidate.string();
  }
  return candidates.front().string();
}

std::string workspaceResponse(const std::string& workspace_id,
                              const std::string& input_path,
                              const std::string& source_path,
                              const std::string& workspace_json) {
  const std::string fields =
      "\"workspace_id\":\"" + jsonEscape(workspace_id) + "\","
      "\"workspace_path\":\"" + jsonEscape(input_path) + "\","
      "\"source_path\":\"" + jsonEscape(source_path) + "\"";
  return appendObjectFields(workspace_json, fields);
}

std::string mergeSnapshot(const std::string& workspace_json, const std::string& snapshot_json) {
  std::ostringstream fields;
  const std::string snapshot = snapshot_json.empty() ? "{}" : snapshot_json;
  fields << "{\"snapshot\":" << snapshot
         << ",\"active_pipeline\":" << (jsonMemberValue(snapshot, "active_pipeline").empty() ? "null" : jsonMemberValue(snapshot, "active_pipeline"))
         << ",\"group_id\":" << (jsonMemberValue(snapshot, "group_id").empty() ? "null" : jsonMemberValue(snapshot, "group_id"))
         << ",\"runtime_state\":" << (jsonMemberValue(snapshot, "runtime_state").empty() ? "null" : jsonMemberValue(snapshot, "runtime_state"))
         << ",\"as_config_payload\":" << (jsonMemberValue(snapshot, "as_config_payload").empty() ? "{}" : jsonMemberValue(snapshot, "as_config_payload"))
         << ",\"debug_file_io\":" << (jsonMemberValue(snapshot, "debug_file_io").empty() ? "[]" : jsonMemberValue(snapshot, "debug_file_io"))
         << "}";
  return upsertObjectMember(updatePipelineNamesFromSnapshot(workspace_json, snapshot), "audio_studio_gui", fields.str());
}

std::string failureResponse(const std::string& stage,
                            const std::string& message,
                            const std::string& request_json,
                            const std::vector<std::string>& details = {}) {
  const std::string node = simpleJsonStringField(request_json, "node_id",
      simpleJsonStringField(request_json, "id", "pipeline"));
  const std::string port = simpleJsonStringField(request_json, "port",
      simpleJsonStringField(request_json, "name", "in"));
  std::ostringstream os;
  os << "{\"ok\":false,\"status\":\"failed\",\"stage\":\"" << jsonEscape(stage) << "\"";
  os << ",\"diagnostics\":[{\"severity\":\"error\","
     << "\"component\":\"config.compile\","
     << "\"node_id\":\"" << jsonEscape(node) << "\","
     << "\"port\":\"" << jsonEscape(port) << "\","
     << "\"message\":\"" << jsonEscape(message.empty() ? "Audio Studio build failed" : message) << "\"";
  if (!details.empty()) {
    os << ",\"details\":[";
    for (size_t i = 0; i < details.size(); ++i) {
      if (i) os << ",";
      os << "\"" << jsonEscape(details[i]) << "\"";
    }
    os << "]";
  }
  os << "}]";
  os << ",\"node_marks\":{\"" << jsonEscape(node) << "\":{\"severity\":\"error\","
     << "\"message\":\"" << jsonEscape(message.empty() ? "build failed" : message) << "\"}}";
  os << ",\"port_marks\":{\"" << jsonEscape(node + ":" + port) << "\":{\"severity\":\"error\","
     << "\"message\":\"" << jsonEscape(message.empty() ? "build failed" : message) << "\"}}";
  os << "}";
  return os.str();
}

class SocketConfigCompileClient final : public IConfigCompileClient {
public:
  GuiConfigCompileResult compile(const GuiConfigCompileRequest& request) override;

private:
  std::string host_ = envString("AUDIO_STUDIO_AS_SERVER_HOST", "127.0.0.1");
  uint16_t port_ = envPort("AUDIO_STUDIO_AS_SERVER_PORT", 9900);
  uint32_t timeout_ms_ = static_cast<uint32_t>(envPort("AUDIO_STUDIO_AS_SERVER_TIMEOUT_MS", 5000));
};

class ProcessValidationRunner final : public IValidationRunner {
public:
  ValidationResult run(const ValidationRequest& request) override;
};

#if !defined(_WIN32)
bool writeAllToSocket(int fd, const std::string& data, std::string& error) {
  size_t written = 0;
  while (written < data.size()) {
    const ssize_t n = send(fd, data.data() + written, data.size() - written, 0);
    if (n < 0) {
      error = std::string("socket write failed: ") + std::strerror(errno);
      return false;
    }
    if (n == 0) {
      error = "socket write failed: peer closed connection";
      return false;
    }
    written += static_cast<size_t>(n);
  }
  return true;
}

bool readByteFromSocket(int fd, char& out, std::string& error) {
  const ssize_t n = recv(fd, &out, 1, 0);
  if (n == 1) return true;
  if (n == 0) error = "socket read failed: peer closed connection";
  else error = std::string("socket read failed: ") + std::strerror(errno);
  return false;
}

bool readContentLengthFrameFromSocket(int fd, std::string& payload, std::string& error) {
  std::string header;
  while (header.find("\r\n\r\n") == std::string::npos) {
    char c = 0;
    if (!readByteFromSocket(fd, c, error)) return false;
    header.push_back(c);
    if (header.size() > 4096) {
      error = "RPC frame header is too large";
      return false;
    }
  }

  const std::string key = "Content-Length:";
  const size_t key_pos = header.find(key);
  if (key_pos == std::string::npos) {
    error = "missing Content-Length header in as_server response";
    return false;
  }
  size_t begin = key_pos + key.size();
  while (begin < header.size() && std::isspace(static_cast<unsigned char>(header[begin]))) ++begin;
  size_t end = begin;
  while (end < header.size() && std::isdigit(static_cast<unsigned char>(header[end]))) ++end;
  if (begin == end) {
    error = "invalid Content-Length header in as_server response";
    return false;
  }

  const size_t header_end = header.find("\r\n\r\n");
  const size_t content_length = static_cast<size_t>(std::stoull(header.substr(begin, end - begin)));
  if (content_length > 16 * 1024 * 1024) {
    error = "as_server response is too large";
    return false;
  }

  payload = header.substr(header_end + 4);
  payload.reserve(content_length);
  while (payload.size() < content_length) {
    char c = 0;
    if (!readByteFromSocket(fd, c, error)) return false;
    payload.push_back(c);
  }
  if (payload.size() > content_length) payload.resize(content_length);
  return true;
}

int connectTcp(const std::string& host, uint16_t port, uint32_t timeout_ms, std::string& error) {
  struct addrinfo hints {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo* result = nullptr;
  const std::string port_text = std::to_string(port);
  const int gai = getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result);
  if (gai != 0) {
    error = std::string("resolve as_server failed: ") + gai_strerror(gai);
    return -1;
  }

  int fd = -1;
  for (auto* rp = result; rp != nullptr; rp = rp->ai_next) {
    fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (fd < 0) continue;

    struct timeval tv {};
    tv.tv_sec = static_cast<time_t>(timeout_ms / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000) * 1000);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
      freeaddrinfo(result);
      return fd;
    }
    error = std::string("connect as_server failed: ") + std::strerror(errno);
    close(fd);
    fd = -1;
  }

  freeaddrinfo(result);
  if (error.empty()) error = "connect as_server failed";
  return -1;
}
#endif

GuiConfigCompileResult SocketConfigCompileClient::compile(const GuiConfigCompileRequest& request) {
  GuiConfigCompileResult result;

#if defined(_WIN32)
  result.ok = false;
  result.message = "as_server socket config.compile is not implemented on Windows GUI backend yet";
  return result;
#else
  std::ostringstream params;
  params << "{"
         << "\"input_path\":\"" << jsonEscape(request.input_path) << "\","
         << "\"output_dir\":\"" << jsonEscape(request.output_dir) << "\","
         << "\"project_name\":\"" << jsonEscape(request.project_name) << "\","
         << "\"alsatplg\":\"" << jsonEscape(request.alsatplg) << "\","
         << "\"build_tplg\":" << (request.build_tplg ? "true" : "false") << ","
         << "\"strict\":" << (request.strict ? "true" : "false") << ","
         << "\"plugin_paths\":" << jsonStringArray(request.plugin_paths)
         << "}";
  const std::string rpc_request =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"config.compile\",\"params\":" + params.str() + "}";
  const std::string frame = "Content-Length: " + std::to_string(rpc_request.size()) +
                            "\r\n\r\n" + rpc_request;

  std::string error;
  const int fd = connectTcp(host_, port_, timeout_ms_, error);
  if (fd < 0) {
    result.ok = false;
    result.message = error.empty() ? "failed to connect as_server config.compile RPC" : error;
    return result;
  }

  std::string response;
  const bool sent = writeAllToSocket(fd, frame, error);
  const bool read = sent && readContentLengthFrameFromSocket(fd, response, error);
  close(fd);
  if (!sent || !read) {
    result.ok = false;
    result.message = error.empty() ? "as_server config.compile RPC failed" : error;
    return result;
  }

  const std::string error_json = jsonMemberValue(response, "error");
  if (!error_json.empty()) {
    result.ok = false;
    result.message = simpleJsonStringField(error_json, "message", "as_server config.compile returned error");
    result.diagnostics.push_back(error_json);
    return result;
  }

  const std::string result_json = jsonMemberValue(response, "result");
  if (result_json.empty()) {
    result.ok = false;
    result.message = "invalid as_server config.compile response: missing result";
    result.diagnostics.push_back(response);
    return result;
  }

  result.ok = result_json.find("\"ok\":true") != std::string::npos;
  result.tplg_path = simpleJsonStringField(result_json, "tplg_path");
  result.message = result.ok ? "as_server config.compile succeeded" : "as_server config.compile returned ok=false";
  for (const auto& warning : simpleJsonStringArrayField(result_json, "warnings")) {
    result.diagnostics.push_back(warning);
  }
  if (result.ok && result.tplg_path.empty()) {
    result.ok = false;
    result.message = "as_server config.compile did not return tplg_path";
  }
  if (!result.ok && result.diagnostics.empty()) result.diagnostics.push_back(result_json);
  return result;
#endif
}

ValidationResult ProcessValidationRunner::run(const ValidationRequest& request) {
  ValidationResult result;
  if (!fs::exists(request.script_path)) {
    result.ok = false;
    result.message = "rv32qemu sof-build-test.py not found: " + request.script_path;
    return result;
  }

  const std::string host = envString("AUDIO_STUDIO_VALIDATION_AS_SERVER_HOST", "127.0.0.1");
  const uint16_t port = envPort("AUDIO_STUDIO_VALIDATION_AS_SERVER_PORT", 9901);
  const std::string log_path = (fs::path(request.workspace_dir) / "audio_studio_validation.log").string();
  const std::string cmd =
      "python3 " + shellQuote(request.script_path) +
      " -t " + shellQuote(request.test_list_path) +
      " --audio-controller-log --as-server-host " + shellQuote(host) +
      " --as-server-port " + std::to_string(port) +
      " > " + shellQuote(log_path) + " 2>&1";

  const int status = std::system(cmd.c_str());
  if (status == 0) {
    result.ok = true;
    result.message = "rv32qemu validation passed";
    return result;
  }

  result.ok = false;
#if !defined(_WIN32)
  if (WIFEXITED(status)) {
    result.message = "rv32qemu validation failed with exit code " + std::to_string(WEXITSTATUS(status));
  } else if (WIFSIGNALED(status)) {
    result.message = "rv32qemu validation killed by signal " + std::to_string(WTERMSIG(status));
  } else
#endif
  {
    result.message = "rv32qemu validation failed";
  }
  result.diagnostics.push_back("validation_log=" + log_path);
  return result;
}

} // namespace

BuildOrchestrator::BuildOrchestrator(std::string root_dir,
                                     std::shared_ptr<IConfigCompileClient> compile_client,
                                     std::shared_ptr<IValidationRunner> validation_runner)
  : root_dir_(std::move(root_dir)),
    compile_client_(std::move(compile_client)),
    validation_runner_(std::move(validation_runner)) {
  if (!compile_client_) compile_client_ = std::make_shared<SocketConfigCompileClient>();
  if (!validation_runner_) validation_runner_ = std::make_shared<ProcessValidationRunner>();
}

BuildOrchestrator::WorkspaceRecord* BuildOrchestrator::findWorkspaceLocked(const std::string& workspace_id) {
  auto it = workspaces_.find(workspace_id);
  return it == workspaces_.end() ? nullptr : &it->second;
}

BuildOrchestrator::WorkspaceRecord& BuildOrchestrator::openProjectLocked(const std::string& project) {
  const std::string sanitized_project = sanitizeProjectConfigPath(project);
  const std::string workspace_id = "ws_" + safeId(sanitized_project);
  auto existing = workspaces_.find(workspace_id);
  if (existing != workspaces_.end()) return existing->second;

  WorkspaceRecord record;
  record.workspace_id = workspace_id;
  record.project = sanitized_project;
  record.project_name = projectNameFromProjectPath(sanitized_project);
  record.source_path = (fs::path(root_dir_) / projectConfigRelPath(sanitized_project)).string();
  record.workspace_dir = (fs::temp_directory_path() / "audio-studio-gui-workspaces" /
                          safeId(root_dir_) / workspace_id).string();
  record.input_path = (fs::path(record.workspace_dir) / "project.json").string();
  record.output_dir = (fs::path(record.workspace_dir) / "build").string();

  fs::create_directories(record.workspace_dir);
  const std::string source_json = readTextFile(record.source_path);
  writeTextFile(record.input_path, source_json);

  auto inserted = workspaces_.emplace(record.workspace_id, std::move(record));
  return inserted.first->second;
}

HttpResponse BuildOrchestrator::openProjectByName(const std::string& project) {
  std::lock_guard<std::mutex> lk(mutex_);
  auto& record = openProjectLocked(project);
  const std::string body = readTextFile(record.input_path);
  if (body.empty()) {
    return {404, "application/json; charset=utf-8",
            "{\"ok\":false,\"error\":\"project config not found\"}"};
  }
  return {200, "application/json; charset=utf-8",
          workspaceResponse(record.workspace_id, record.input_path, record.source_path, body)};
}

HttpResponse BuildOrchestrator::openProjectRequest(const std::string& request_json) {
  const std::string project = simpleJsonStringField(request_json, "project", "a2/A2.json");
  return openProjectByName(project);
}

HttpResponse BuildOrchestrator::buildPipeline(const std::string& request_json) {
  WorkspaceRecord record;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    std::string workspace_id = simpleJsonStringField(request_json, "workspace_id");
    if (workspace_id.empty()) {
      auto& opened = openProjectLocked(simpleJsonStringField(request_json, "project", "a2/A2.json"));
      workspace_id = opened.workspace_id;
    }
    auto* found = findWorkspaceLocked(workspace_id);
    if (!found) {
      return {404, "application/json; charset=utf-8",
              failureResponse("open", "workspace_id not found", request_json)};
    }
    record = *found;
  }

  const std::string snapshot = jsonMemberValue(request_json, "snapshot");
  const std::string current_json = readTextFile(record.input_path);
  if (current_json.empty()) {
    return {404, "application/json; charset=utf-8",
            failureResponse("open", "workspace project JSON is missing", request_json)};
  }
  if (!writeTextFile(record.input_path, mergeSnapshot(current_json, snapshot.empty() ? "{}" : snapshot))) {
    return {500, "application/json; charset=utf-8",
            failureResponse("workspace", "failed to update workspace JSON", request_json)};
  }

  GuiConfigCompileRequest compile_request;
  compile_request.input_path = record.input_path;
  compile_request.output_dir = record.output_dir;
  compile_request.project_name = record.project_name;
  compile_request.alsatplg = (fs::path(root_dir_) / "third_party/alsatplg/bin/alsatplg").string();
  compile_request.build_tplg = true;
  compile_request.strict = true;
  compile_request.plugin_paths = {};

  auto compile_result = compile_client_->compile(compile_request);
  if (!compile_result.ok) {
    return {200, "application/json; charset=utf-8",
            failureResponse("compile", compile_result.message, request_json, compile_result.diagnostics)};
  }

  fs::create_directories(record.output_dir);
  const std::string test_list_path = (fs::path(record.output_dir) / "audio_studio_test_list.txt").string();
  const std::string test_list =
      "ac_run --endpoint as_datalink --mtu 512\n"
      "trace on\n"
      "pipeinstall " + compile_result.tplg_path + "\n"
      "ac_run --stop\n";
  if (!writeTextFile(test_list_path, test_list)) {
    return {500, "application/json; charset=utf-8",
            failureResponse("validation", "failed to write audio_studio_test_list", request_json)};
  }

  ValidationRequest validation_request;
  validation_request.workspace_id = record.workspace_id;
  validation_request.workspace_dir = record.workspace_dir;
  validation_request.project_name = record.project_name;
  validation_request.tplg_path = compile_result.tplg_path;
  validation_request.test_list_path = test_list_path;
  validation_request.script_path = resolveValidationScriptPath(root_dir_);

  auto validation_result = validation_runner_->run(validation_request);
  if (!validation_result.ok) {
    return {200, "application/json; charset=utf-8",
            failureResponse("validation", validation_result.message, request_json, validation_result.diagnostics)};
  }

  {
    std::lock_guard<std::mutex> lk(mutex_);
    auto* found = findWorkspaceLocked(record.workspace_id);
    if (found) found->build_ok = true;
  }

  std::ostringstream os;
  os << "{\"ok\":true,\"status\":\"PIPE_LOADED\",\"runtime_state\":\"PIPE_LOADED\""
     << ",\"workspace_id\":\"" << jsonEscape(record.workspace_id) << "\""
     << ",\"tplg_path\":\"" << jsonEscape(compile_result.tplg_path) << "\""
     << ",\"test_list_path\":\"" << jsonEscape(test_list_path) << "\""
     << ",\"validation\":\"" << jsonEscape(validation_result.message) << "\"}";
  return {200, "application/json; charset=utf-8", os.str()};
}

HttpResponse BuildOrchestrator::saveProject(const std::string& request_json) {
  WorkspaceRecord record;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    const std::string workspace_id = simpleJsonStringField(request_json, "workspace_id");
    auto* found = findWorkspaceLocked(workspace_id);
    if (!found) {
      return {409, "application/json; charset=utf-8",
              "{\"ok\":false,\"status\":\"failed\",\"stage\":\"save\",\"error\":\"build_required\",\"diagnostics\":[\"workspace_id must be opened and built before save\"]}"};
    }
    if (!found->build_ok) {
      return {409, "application/json; charset=utf-8",
              "{\"ok\":false,\"status\":\"failed\",\"stage\":\"save\",\"error\":\"build_required\",\"diagnostics\":[\"project must build successfully before save\"]}"};
    }
    record = *found;
  }

  const std::string workspace_json = readTextFile(record.input_path);
  if (workspace_json.empty() || !writeTextFile(record.source_path, workspace_json)) {
    return {500, "application/json; charset=utf-8",
            "{\"ok\":false,\"status\":\"failed\",\"stage\":\"save\",\"error\":\"write_failed\",\"diagnostics\":[\"failed to write source config JSON\"]}"};
  }
  return {200, "application/json; charset=utf-8",
          "{\"ok\":true,\"status\":\"saved\",\"source_path\":\"" + jsonEscape(record.source_path) + "\"}"};
}

} // namespace audiostudio
