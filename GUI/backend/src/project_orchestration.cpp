#include "audio_studio.hpp"

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <deque>
#include <set>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#include "autoconfig.h"
#include "json_value.hpp"

#if defined(CONFIG_RPC_CLIENT) && defined(CONFIG_RPC_TRANSPORT_SOCKET) && defined(CONFIG_DRIVER_SOCKET)
#include "audio_rpc_client.hpp"
#include "driver_manager.hpp"
#include "rpc_socket_transport.hpp"
#define AUDIO_STUDIO_GUI_PLAYBACK_RPC 1
#endif

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
std::string mergeSnapshot(const std::string& workspace_json, const std::string& snapshot_json);
std::string jsonArrayFromObjects(const std::vector<std::string>& objects);

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

std::string fileIoDaiPath(const std::string& output_dir,
                          const char* stem,
                          uint32_t dai_index) {
  std::ostringstream name;
  name << stem;
  if (dai_index != 0u) name << ".dai" << dai_index;
  name << ".wav";
  return (fs::path(output_dir) / name.str()).string();
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

std::string lowerAscii(std::string value) {
  for (char& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return value;
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

std::string directionRuntimeRequest(const std::string& request_json, const std::string& field) {
  std::string sub_request = jsonMemberValue(request_json, field);
  if (sub_request.empty() || sub_request.front() != '{') return request_json;

  std::ostringstream inherited;
  const std::vector<std::string> keys = {
    "session_id",
    "as_server_host",
    "as_server_port",
    "driver_factory",
  };
  bool first = true;
  for (const auto& key : keys) {
    if (!jsonMemberValue(sub_request, key).empty()) continue;
    const auto value = jsonMemberValue(request_json, key);
    if (value.empty()) continue;
    if (!first) inherited << ",";
    inherited << "\"" << key << "\":" << value;
    first = false;
  }

  const auto fields = inherited.str();
  return fields.empty() ? sub_request : appendObjectFields(sub_request, fields);
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

std::string removeTopLevelObjectMember(const std::string& object_json, const std::string& field) {
  size_t begin = 0;
  size_t end = 0;
  if (!topLevelMemberRange(object_json, field, begin, end)) return object_json;
  std::string out = object_json;
  out.erase(begin, end - begin);
  return out;
}

std::string stripGuiOnlySectionsForSave(const std::string& workspace_json) {
  return removeTopLevelObjectMember(workspace_json, "audio_studio_gui");
}

std::string topLevelJsonMemberValue(const std::string& object_json, const std::string& field) {
  size_t begin = 0;
  size_t end = 0;
  if (!topLevelMemberRange(object_json, field, begin, end)) return {};
  return jsonMemberValue(object_json.substr(begin, end - begin), field);
}

std::vector<std::string> jsonObjectArrayItems(const std::string& array_json) {
  std::vector<std::string> out;
  if (array_json.empty() || array_json.front() != '[') return out;
  for (size_t i = 1; i < array_json.size();) {
    while (i < array_json.size() && array_json[i] != '{' && array_json[i] != ']') ++i;
    if (i >= array_json.size() || array_json[i] == ']') break;
    const size_t object_end = matchingJsonValueEnd(array_json, i);
    if (object_end == std::string::npos) break;
    out.push_back(array_json.substr(i, object_end - i));
    i = object_end;
  }
  return out;
}

bool simpleJsonBoolField(const std::string& body, const std::string& field, bool fallback = false) {
  const std::string value = jsonMemberValue(body, field);
  if (value.empty()) return fallback;
  if (value.compare(0, 4, "true") == 0) return true;
  if (value.compare(0, 5, "false") == 0) return false;
  return fallback;
}

int simpleJsonIntField(const std::string& body, const std::string& field, int fallback) {
  std::string value = jsonMemberValue(body, field);
  if (value.empty()) return fallback;
  if (!value.empty() && value.front() == '"') value = simpleJsonStringField(body, field);
  try {
    return value.empty() ? fallback : std::stoi(value);
  } catch (...) {
    return fallback;
  }
}

std::string jsonStringFieldAny(const std::string& object_json,
                               const std::vector<std::string>& fields,
                               const std::string& fallback = "") {
  for (const auto& field : fields) {
    const std::string value = simpleJsonStringField(object_json, field);
    if (!value.empty()) return value;
  }
  return fallback;
}

std::map<std::string, std::string> simpleJsonStringMapField(const std::string& object_json,
                                                            const std::string& field) {
  std::map<std::string, std::string> out;
  const std::string object = jsonMemberValue(object_json, field);
  if (object.empty() || object.front() != '{') return out;
  for (size_t i = 1; i < object.size();) {
    while (i < object.size() && object[i] != '"' && object[i] != '}') ++i;
    if (i >= object.size() || object[i] == '}') break;
    const size_t key_start = i;
    const size_t key_end = matchingJsonValueEnd(object, key_start);
    if (key_end == std::string::npos) break;
    const std::string key_json = object.substr(key_start, key_end - key_start);
    i = object.find(':', key_end);
    if (i == std::string::npos) break;
    ++i;
    while (i < object.size() && std::isspace(static_cast<unsigned char>(object[i]))) ++i;
    if (i >= object.size() || object[i] != '"') {
      const size_t value_end = matchingJsonValueEnd(object, i);
      if (value_end == std::string::npos) break;
      i = value_end;
      continue;
    }
    const size_t value_end = matchingJsonValueEnd(object, i);
    if (value_end == std::string::npos) break;
    const std::string value_json = object.substr(i, value_end - i);
    std::string value;
    bool esc = false;
    for (size_t j = 1; j + 1 < value_json.size(); ++j) {
      const char c = value_json[j];
      if (esc) {
        value.push_back(c);
        esc = false;
      } else if (c == '\\') {
        esc = true;
      } else {
        value.push_back(c);
      }
    }
    std::string decoded_key;
    esc = false;
    for (size_t j = 1; j + 1 < key_json.size(); ++j) {
      const char c = key_json[j];
      if (esc) {
        decoded_key.push_back(c);
        esc = false;
      } else if (c == '\\') {
        esc = true;
      } else {
        decoded_key.push_back(c);
      }
    }
    out[decoded_key] = value;
    i = value_end;
  }
  return out;
}

std::string canonicalPortDomain(const std::string& value) {
  std::string lower;
  lower.reserve(value.size());
  for (char c : value) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return lower == "external" ? "external" : "sof";
}

struct SnapshotNode {
  std::string graph_id;
  std::string name;
  std::string module_type;
  std::string pipeline_id;
  std::string pipeline_node_id;
  std::string params_json = "{}";
  bool debug_file_io = false;
  std::map<std::string, std::string> port_domains;
};

struct SnapshotConnection {
  std::string from_node;
  std::string from_port;
  std::string to_node;
  std::string to_port;
  std::string from_domain;
  std::string to_domain;
};

struct SnapshotGroup {
  std::string id;
  std::string pipeline_id;
  std::string name;
  std::vector<std::string> origin_pipeline_ids;
  std::vector<std::string> node_ids;
};

struct RegeneratedProject {
  std::string pipelines_json = "[]";
  std::string frontend_connections_json = "[]";
  std::set<std::string> generated_node_keys;
  std::vector<std::string> pipeline_ids;
};

bool isDebugFileModule(const std::string& module_type) {
  std::string id;
  id.reserve(module_type.size());
  for (char c : module_type) id.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return id == "builtin.file_input" || id == "builtin.file_output";
}

std::string graphLocalNodeId(const SnapshotNode& node, const std::string& pipe_id) {
  if (!node.pipeline_node_id.empty()) return node.pipeline_node_id;
  const std::string marker = "__";
  const size_t marker_pos = node.graph_id.find(marker);
  if (marker_pos != std::string::npos) return node.graph_id.substr(marker_pos + marker.size());
  const std::string prefix = safeId(pipe_id) + marker;
  if (node.graph_id.compare(0, prefix.size(), prefix) == 0) return node.graph_id.substr(prefix.size());
  return safeId(node.graph_id);
}

bool splitGraphEndpoint(const std::string& endpoint, std::string& node_id, std::string& port) {
  size_t sep = endpoint.rfind('.');
  if (sep == std::string::npos) sep = endpoint.rfind(':');
  if (sep == std::string::npos || sep == 0 || sep + 1 >= endpoint.size()) return false;
  node_id = endpoint.substr(0, sep);
  port = endpoint.substr(sep + 1);
  return true;
}

std::string nodePortDomain(const SnapshotNode& node, const std::string& port) {
  const auto it = node.port_domains.find(port);
  return canonicalPortDomain(it == node.port_domains.end() ? "sof" : it->second);
}

std::map<std::string, std::string> originalPipelineObjects(const std::string& workspace_json) {
  std::map<std::string, std::string> out;
  for (const auto& object : jsonObjectArrayItems(topLevelJsonMemberValue(workspace_json, "pipelines"))) {
    const std::string pipe_id = simpleJsonStringField(object, "pipe_id");
    if (!pipe_id.empty()) out[pipe_id] = object;
  }
  return out;
}

std::string pipelineObjectId(const std::string& pipeline_json) {
  const std::string pipe_id = simpleJsonStringField(pipeline_json, "pipe_id");
  return pipe_id.empty() ? simpleJsonStringField(pipeline_json, "name") : pipe_id;
}

std::string frontendConnectionPipelineId(const std::string& frontend_json) {
  const std::string pipeline_id = jsonStringFieldAny(frontend_json, {"pipeline_id", "pipelineId"});
  return pipeline_id.empty() ? simpleJsonStringField(frontend_json, "pipe_id") : pipeline_id;
}

std::set<std::string> frontendConnectionPipelineIds(const std::string& workspace_json) {
  std::set<std::string> ids;
  for (const auto& frontend_json : jsonObjectArrayItems(topLevelJsonMemberValue(workspace_json, "frontend_connections"))) {
    const std::string pipe_id = frontendConnectionPipelineId(frontend_json);
    if (!pipe_id.empty()) ids.insert(pipe_id);
  }
  return ids;
}

void collectPipelineNodeKeys(const std::string& pipeline_json,
                             std::set<std::string>& generated_node_keys) {
  const std::string pipe_id = pipelineObjectId(pipeline_json);
  if (pipe_id.empty()) return;
  for (const auto& node_json : jsonObjectArrayItems(jsonMemberValue(pipeline_json, "nodes"))) {
    const std::string node_id = simpleJsonStringField(node_json, "node_id",
        simpleJsonStringField(node_json, "id"));
    if (!node_id.empty()) generated_node_keys.insert(pipe_id + "." + node_id);
  }
}

bool isAllPipelinesBuildRequest(const std::string& request_json,
                                const std::string& snapshot_json) {
  (void)request_json;
  (void)snapshot_json;
  return true;
}

std::set<std::string> pipelineNodeIds(const std::string& pipeline_json) {
  std::set<std::string> ids;
  for (const auto& node_json : jsonObjectArrayItems(jsonMemberValue(pipeline_json, "nodes"))) {
    const std::string node_id = simpleJsonStringField(node_json, "node_id",
        simpleJsonStringField(node_json, "id"));
    if (!node_id.empty()) ids.insert(node_id);
  }
  return ids;
}

std::set<std::string> objectEdgeKeys(const std::string& object_json) {
  std::set<std::string> keys;
  for (const auto& edge_json : jsonObjectArrayItems(jsonMemberValue(object_json, "edges"))) {
    const std::string from = simpleJsonStringField(edge_json, "from");
    const std::string to = simpleJsonStringField(edge_json, "to");
    if (!from.empty() && !to.empty()) keys.insert(from + "->" + to);
  }
  return keys;
}

bool regeneratedObjectCoversSource(const std::string& source_object,
                                   const std::string& regenerated_object) {
  const auto source_ids = pipelineNodeIds(source_object);
  const auto regenerated_ids = pipelineNodeIds(regenerated_object);
  for (const auto& node_id : source_ids) {
    if (!regenerated_ids.count(node_id)) return false;
  }

  const auto source_edges = objectEdgeKeys(source_object);
  const auto regenerated_edges = objectEdgeKeys(regenerated_object);
  for (const auto& edge : source_edges) {
    if (!regenerated_edges.count(edge)) return false;
  }

  return !source_ids.empty() || source_edges.empty();
}

bool regeneratedPipelineCoversSource(const std::string& source_pipeline,
                                     const std::string& regenerated_pipeline) {
  return regeneratedObjectCoversSource(source_pipeline, regenerated_pipeline);
}

std::map<std::string, std::string> pipelineNodesById(const std::string& pipeline_json) {
  std::map<std::string, std::string> nodes;
  for (const auto& node_json : jsonObjectArrayItems(jsonMemberValue(pipeline_json, "nodes"))) {
    const std::string node_id = simpleJsonStringField(node_json, "node_id",
        simpleJsonStringField(node_json, "id"));
    if (!node_id.empty()) nodes[node_id] = node_json;
  }
  return nodes;
}

std::string mergeSourceNodeWithRegeneratedNode(const std::string& source_node,
                                               const std::string& regenerated_node) {
  std::string out = source_node;
  const std::string name = jsonMemberValue(regenerated_node, "name");
  if (!name.empty()) out = upsertObjectMember(out, "name", name);
  const std::string params = jsonMemberValue(regenerated_node, "params");
  if (!params.empty()) out = upsertObjectMember(out, "params", params);
  return out;
}

std::string mergeSourcePipelineWithRegeneratedNodeParams(const std::string& source_pipeline,
                                                         const std::string& regenerated_pipeline) {
  const auto regenerated_nodes = pipelineNodesById(regenerated_pipeline);
  std::vector<std::string> merged_nodes;
  for (const auto& source_node : jsonObjectArrayItems(jsonMemberValue(source_pipeline, "nodes"))) {
    const std::string node_id = simpleJsonStringField(source_node, "node_id",
        simpleJsonStringField(source_node, "id"));
    const auto it = regenerated_nodes.find(node_id);
    merged_nodes.push_back(it == regenerated_nodes.end()
        ? source_node
        : mergeSourceNodeWithRegeneratedNode(source_node, it->second));
  }

  std::string out = source_pipeline;
  const std::string name = jsonMemberValue(regenerated_pipeline, "name");
  if (!name.empty()) out = upsertObjectMember(out, "name", name);
  out = upsertObjectMember(out, "nodes", jsonArrayFromObjects(merged_nodes));
  return out;
}

std::string mergeSourceFrontendWithRegeneratedNodeParams(const std::string& source_frontend,
                                                         const std::string& regenerated_frontend) {
  const auto regenerated_nodes = pipelineNodesById(regenerated_frontend);
  std::vector<std::string> merged_nodes;
  for (const auto& source_node : jsonObjectArrayItems(jsonMemberValue(source_frontend, "nodes"))) {
    const std::string node_id = simpleJsonStringField(source_node, "node_id",
        simpleJsonStringField(source_node, "id"));
    const auto it = regenerated_nodes.find(node_id);
    merged_nodes.push_back(it == regenerated_nodes.end()
        ? source_node
        : mergeSourceNodeWithRegeneratedNode(source_node, it->second));
  }

  std::string out = source_frontend;
  out = upsertObjectMember(out, "nodes", jsonArrayFromObjects(merged_nodes));
  return out;
}

void mergeSourcePipelinesForAllBuild(const std::string& workspace_json,
                                     RegeneratedProject& regenerated) {
  const auto source_pipelines = jsonObjectArrayItems(topLevelJsonMemberValue(workspace_json, "pipelines"));
  const auto regenerated_pipelines = jsonObjectArrayItems(regenerated.pipelines_json);
  if (source_pipelines.empty()) return;
  const std::set<std::string> layout_pipeline_ids = frontendConnectionPipelineIds(workspace_json);
  const bool restrict_to_layout = !layout_pipeline_ids.empty();

  std::map<std::string, std::string> regenerated_by_id;
  std::vector<std::string> regenerated_order;
  for (const auto& pipeline_json : regenerated_pipelines) {
    const std::string pipe_id = pipelineObjectId(pipeline_json);
    if (pipe_id.empty()) continue;
    regenerated_by_id[pipe_id] = pipeline_json;
    regenerated_order.push_back(pipe_id);
  }

  std::vector<std::string> merged_pipelines;
  std::vector<std::string> merged_ids;
  std::set<std::string> emitted_ids;
  for (const auto& source_pipeline : source_pipelines) {
    const std::string pipe_id = pipelineObjectId(source_pipeline);
    if (pipe_id.empty()) continue;
    if (restrict_to_layout && !layout_pipeline_ids.count(pipe_id) && !regenerated_by_id.count(pipe_id)) continue;
    const auto replacement = regenerated_by_id.find(pipe_id);
    const std::string chosen = replacement == regenerated_by_id.end()
        ? source_pipeline
        : (regeneratedPipelineCoversSource(source_pipeline, replacement->second)
            ? replacement->second
            : mergeSourcePipelineWithRegeneratedNodeParams(source_pipeline, replacement->second));
    merged_pipelines.push_back(chosen);
    merged_ids.push_back(pipe_id);
    emitted_ids.insert(pipe_id);
    collectPipelineNodeKeys(chosen, regenerated.generated_node_keys);
  }
  (void)regenerated_order;

  std::map<std::string, std::string> regenerated_frontend_by_id;
  std::vector<std::string> regenerated_frontend_order;
  for (const auto& frontend_json : jsonObjectArrayItems(regenerated.frontend_connections_json)) {
    const std::string pipe_id = frontendConnectionPipelineId(frontend_json);
    if (pipe_id.empty()) continue;
    regenerated_frontend_by_id[pipe_id] = frontend_json;
    regenerated_frontend_order.push_back(pipe_id);
  }
  std::map<std::string, std::string> source_frontend_by_id;
  for (const auto& frontend_json : jsonObjectArrayItems(topLevelJsonMemberValue(workspace_json, "frontend_connections"))) {
    const std::string pipe_id = frontendConnectionPipelineId(frontend_json);
    if (!pipe_id.empty()) source_frontend_by_id[pipe_id] = frontend_json;
  }

  std::vector<std::string> merged_frontend;
  std::set<std::string> emitted_frontend_ids;
  for (const auto& pipe_id : merged_ids) {
    const auto regenerated_it = regenerated_frontend_by_id.find(pipe_id);
    const auto source_it = source_frontend_by_id.find(pipe_id);
    if (regenerated_it == regenerated_frontend_by_id.end() && source_it == source_frontend_by_id.end()) continue;
    if (regenerated_it == regenerated_frontend_by_id.end()) {
      merged_frontend.push_back(source_it->second);
    } else if (source_it == source_frontend_by_id.end() ||
               regeneratedObjectCoversSource(source_it->second, regenerated_it->second)) {
      merged_frontend.push_back(regenerated_it->second);
    } else {
      merged_frontend.push_back(
          mergeSourceFrontendWithRegeneratedNodeParams(source_it->second, regenerated_it->second));
    }
    emitted_frontend_ids.insert(pipe_id);
  }
  for (const auto& pipe_id : regenerated_frontend_order) {
    if (emitted_frontend_ids.count(pipe_id)) continue;
    const auto it = regenerated_frontend_by_id.find(pipe_id);
    if (it != regenerated_frontend_by_id.end()) merged_frontend.push_back(it->second);
  }

  regenerated.pipelines_json = jsonArrayFromObjects(merged_pipelines);
  regenerated.frontend_connections_json = jsonArrayFromObjects(merged_frontend);
  regenerated.pipeline_ids = std::move(merged_ids);
}

std::string mergeTopLevelObjectArrayById(const std::string& current_json,
                                         const std::string& source_json,
                                         const std::string& field,
                                         const std::string& id_field) {
  std::vector<std::string> objects;
  std::map<std::string, size_t> index_by_id;
  auto add_objects = [&](const std::string& owner_json) {
    for (const auto& object : jsonObjectArrayItems(topLevelJsonMemberValue(owner_json, field))) {
      const std::string id = simpleJsonStringField(object, id_field);
      if (id.empty()) {
        objects.push_back(object);
        continue;
      }
      const auto it = index_by_id.find(id);
      if (it == index_by_id.end()) {
        index_by_id[id] = objects.size();
        objects.push_back(object);
      } else {
        objects[it->second] = object;
      }
    }
  };
  add_objects(source_json);
  add_objects(current_json);
  return jsonArrayFromObjects(objects);
}

std::string workspaceJsonWithSourceBaseline(const std::string& workspace_json,
                                            const std::string& source_json) {
  if (source_json.empty()) return workspace_json;
  std::string out = workspace_json;
  out = upsertObjectMember(out, "pipelines",
      mergeTopLevelObjectArrayById(workspace_json, source_json, "pipelines", "pipe_id"));
  out = upsertObjectMember(out, "frontend_connections",
      mergeTopLevelObjectArrayById(workspace_json, source_json, "frontend_connections", "pipeline_id"));
  return out;
}

std::vector<SnapshotNode> snapshotNodes(const std::string& snapshot_json) {
  std::vector<SnapshotNode> out;
  for (const auto& object : jsonObjectArrayItems(topLevelJsonMemberValue(snapshot_json, "nodes"))) {
    SnapshotNode node;
    node.graph_id = simpleJsonStringField(object, "id");
    if (node.graph_id.empty()) continue;
    node.name = simpleJsonStringField(object, "name", node.graph_id);
    node.module_type = simpleJsonStringField(object, "module_type");
    node.pipeline_id = jsonStringFieldAny(object, {"pipelineId", "pipeline_id"});
    node.pipeline_node_id = jsonStringFieldAny(object, {"pipelineNodeId", "pipeline_node_id"});
    node.params_json = jsonMemberValue(object, "params");
    if (node.params_json.empty()) node.params_json = "{}";
    node.debug_file_io = simpleJsonBoolField(object, "debug_file_io") || isDebugFileModule(node.module_type);
    node.port_domains = simpleJsonStringMapField(object, "port_domains");
    out.push_back(std::move(node));
  }
  return out;
}

std::vector<SnapshotConnection> snapshotConnections(const std::string& snapshot_json) {
  std::vector<SnapshotConnection> out;
  for (const auto& object : jsonObjectArrayItems(topLevelJsonMemberValue(snapshot_json, "connections"))) {
    SnapshotConnection conn;
    const std::string from = simpleJsonStringField(object, "from");
    const std::string to = simpleJsonStringField(object, "to");
    if (!splitGraphEndpoint(from, conn.from_node, conn.from_port)) continue;
    if (!splitGraphEndpoint(to, conn.to_node, conn.to_port)) continue;
    conn.from_domain = simpleJsonStringField(object, "from_domain");
    conn.to_domain = simpleJsonStringField(object, "to_domain");
    out.push_back(std::move(conn));
  }
  return out;
}

std::vector<SnapshotGroup> snapshotGroups(const std::string& snapshot_json) {
  std::vector<SnapshotGroup> out;
  int index = 1;
  for (const auto& object : jsonObjectArrayItems(topLevelJsonMemberValue(snapshot_json, "working_groups"))) {
    SnapshotGroup group;
    group.id = simpleJsonStringField(object, "id", "GUI_PIPE_" + std::to_string(index));
    group.pipeline_id = jsonStringFieldAny(object, {"pipeline_id", "pipelineId"}, group.id);
    group.name = jsonStringFieldAny(object, {"name", "displayName"}, "GUI Pipeline " + std::to_string(index));
    group.origin_pipeline_ids = simpleJsonStringArrayField(object, "origin_pipeline_ids");
    if (group.origin_pipeline_ids.empty()) group.origin_pipeline_ids = simpleJsonStringArrayField(object, "originPipelineIds");
    group.node_ids = simpleJsonStringArrayField(object, "nodes");
    out.push_back(std::move(group));
    ++index;
  }
  return out;
}

std::string pipelineFieldOrDefault(const std::map<std::string, std::string>& originals,
                                   const std::string& origin_pipe_id,
                                   const std::string& field,
                                   const std::string& fallback) {
  const auto it = originals.find(origin_pipe_id);
  if (it == originals.end()) return fallback;
  const std::string value = jsonMemberValue(it->second, field);
  return value.empty() ? fallback : value;
}

std::string firstPipelineFieldOrDefault(const std::map<std::string, std::string>& originals,
                                        const std::string& field,
                                        const std::string& fallback) {
  for (const auto& [_, object] : originals) {
    const std::string value = jsonMemberValue(object, field);
    if (!value.empty()) return value;
  }
  return fallback;
}

bool regenerateProjectFromSnapshot(const std::string& workspace_json,
                                    const std::string& snapshot_json,
                                    RegeneratedProject& regenerated,
                                    std::vector<std::string>& diagnostics) {
  const auto originals = originalPipelineObjects(workspace_json);
  const auto nodes = snapshotNodes(snapshot_json);
  const auto connections = snapshotConnections(snapshot_json);
  auto groups = snapshotGroups(snapshot_json);
  if (groups.empty()) {
    diagnostics.push_back("snapshot.working_groups is required to regenerate platform pipelines");
    return false;
  }

  std::map<std::string, SnapshotNode> node_by_graph_id;
  for (const auto& node : nodes) node_by_graph_id[node.graph_id] = node;

  const std::string runtime_fallback = firstPipelineFieldOrDefault(originals, "runtime",
      "{\"core_ref\":\"audio_core0\",\"priority\":0,\"clock\":\"timer\"}");
  std::ostringstream pipelines;
  pipelines << "[";
  std::vector<std::string> frontend_connection_jsons;

  for (size_t gi = 0; gi < groups.size(); ++gi) {
    auto& group = groups[gi];
    if (gi) pipelines << ",";

    std::string origin_pipe_id;
    if (group.origin_pipeline_ids.size() == 1 && originals.count(group.origin_pipeline_ids.front())) {
      origin_pipe_id = group.origin_pipeline_ids.front();
    } else if (originals.count(group.pipeline_id)) {
      origin_pipe_id = group.pipeline_id;
    }
    const std::string pipe_id = !origin_pipe_id.empty()
      ? origin_pipe_id
      : (!group.pipeline_id.empty() ? group.pipeline_id : ("GUI_PIPE_" + std::to_string(gi + 1)));
    const std::string pipe_name = !group.name.empty()
      ? group.name
      : (origin_pipe_id.empty() ? ("GUI Pipeline " + std::to_string(gi + 1))
                                : simpleJsonStringField(originals.at(origin_pipe_id), "name", origin_pipe_id));
    regenerated.pipeline_ids.push_back(pipe_id);

    if (group.node_ids.empty()) {
      for (const auto& node : nodes) {
        if (node.pipeline_id == group.pipeline_id || node.pipeline_id == pipe_id ||
            (!origin_pipe_id.empty() && node.pipeline_id == origin_pipe_id)) {
          group.node_ids.push_back(node.graph_id);
        }
      }
    }

    std::map<std::string, std::string> graph_to_local;
    std::set<std::string> group_graph_ids;
    std::set<std::string> debug_graph_ids;
    std::vector<std::string> node_jsons;
    std::vector<std::string> ui_nodes_json;
    for (const auto& graph_id : group.node_ids) {
      const auto it = node_by_graph_id.find(graph_id);
      if (it == node_by_graph_id.end()) continue;
      const SnapshotNode& node = it->second;
      if (node.module_type.empty()) {
        diagnostics.push_back("pipeline node requires module_type: " + node.graph_id);
        return false;
      }
      const std::string local_id = graphLocalNodeId(node, pipe_id);
      graph_to_local[node.graph_id] = local_id;
      group_graph_ids.insert(node.graph_id);
      std::ostringstream node_os;
      node_os << "{\"node_id\":\"" << jsonEscape(local_id) << "\","
              << "\"name\":\"" << jsonEscape(node.name.empty() ? local_id : node.name) << "\","
              << "\"module_type\":\"" << jsonEscape(node.module_type) << "\","
              << "\"params\":" << (node.params_json.empty() ? "{}" : node.params_json) << "}";
      if (node.debug_file_io) {
        debug_graph_ids.insert(node.graph_id);
        ui_nodes_json.push_back(node_os.str());
      } else {
        regenerated.generated_node_keys.insert(pipe_id + "." + local_id);
        node_jsons.push_back(node_os.str());
      }
    }

    std::vector<std::string> edge_jsons;
    std::vector<std::string> frontend_edge_jsons;
    auto frontend_port = [&](const SnapshotNode& node, const std::string& port) {
      if (node.module_type == "builtin.file_input") return std::string("out");
      if (node.module_type == "builtin.file_output") return std::string("in");
      return port;
    };
    for (const auto& conn : connections) {
      if (!group_graph_ids.count(conn.from_node) || !group_graph_ids.count(conn.to_node)) continue;
      const auto from_node_it = node_by_graph_id.find(conn.from_node);
      const auto to_node_it = node_by_graph_id.find(conn.to_node);
      if (from_node_it == node_by_graph_id.end() || to_node_it == node_by_graph_id.end()) continue;
      const std::string from_domain = canonicalPortDomain(conn.from_domain.empty()
        ? nodePortDomain(from_node_it->second, conn.from_port)
        : conn.from_domain);
      const std::string to_domain = canonicalPortDomain(conn.to_domain.empty()
        ? nodePortDomain(to_node_it->second, conn.to_port)
        : conn.to_domain);
      const bool frontend_edge = debug_graph_ids.count(conn.from_node) || debug_graph_ids.count(conn.to_node) ||
                                 from_domain == "external" || to_domain == "external";
      if (frontend_edge) {
        frontend_edge_jsons.push_back("{\"from\":\"" +
            jsonEscape(graph_to_local[conn.from_node] + ":" + frontend_port(from_node_it->second, conn.from_port)) +
            "\",\"to\":\"" +
            jsonEscape(graph_to_local[conn.to_node] + ":" + frontend_port(to_node_it->second, conn.to_port)) + "\"}");
      } else if (from_domain == "sof" && to_domain == "sof") {
        edge_jsons.push_back("{\"from\":\"" + jsonEscape(graph_to_local[conn.from_node] + ":" + conn.from_port) +
                             "\",\"to\":\"" + jsonEscape(graph_to_local[conn.to_node] + ":" + conn.to_port) + "\"}");
      }
    }

    pipelines << "{\"pipe_id\":\"" << jsonEscape(pipe_id) << "\","
              << "\"name\":\"" << jsonEscape(pipe_name) << "\","
              << "\"domain\":" << pipelineFieldOrDefault(originals, origin_pipe_id, "domain", "\"playback\"") << ","
              << "\"frame\":" << pipelineFieldOrDefault(originals, origin_pipe_id, "frame", "{\"block_ms\":4}") << ","
              << "\"runtime\":" << pipelineFieldOrDefault(originals, origin_pipe_id, "runtime", runtime_fallback) << ","
              << "\"nodes\":[";
    for (size_t i = 0; i < node_jsons.size(); ++i) {
      if (i) pipelines << ",";
      pipelines << node_jsons[i];
    }
    pipelines << "],\"edges\":[";
    for (size_t i = 0; i < edge_jsons.size(); ++i) {
      if (i) pipelines << ",";
      pipelines << edge_jsons[i];
    }
    pipelines << "]}";
    if (!ui_nodes_json.empty() || !frontend_edge_jsons.empty()) {
      std::ostringstream frontend;
      frontend << "{\"pipeline_id\":\"" << jsonEscape(pipe_id) << "\",\"nodes\":[";
      for (size_t i = 0; i < ui_nodes_json.size(); ++i) {
        if (i) frontend << ",";
        frontend << ui_nodes_json[i];
      }
      frontend << "],\"edges\":[";
      for (size_t i = 0; i < frontend_edge_jsons.size(); ++i) {
        if (i) frontend << ",";
        frontend << frontend_edge_jsons[i];
      }
      frontend << "]}";
      frontend_connection_jsons.push_back(frontend.str());
    }
  }
  pipelines << "]";
  regenerated.pipelines_json = pipelines.str();
  regenerated.frontend_connections_json = jsonArrayFromObjects(frontend_connection_jsons);
  return true;
}

std::string filterPresetNodeValues(const std::string& workspace_json,
                                   const std::set<std::string>& generated_node_keys) {
  const std::string presets = topLevelJsonMemberValue(workspace_json, "presets");
  if (presets.empty() || presets.front() != '[') return workspace_json;

  std::vector<std::string> preset_jsons;
  for (const auto& preset : jsonObjectArrayItems(presets)) {
    const std::string node_values = jsonMemberValue(preset, "node_values");
    if (node_values.empty() || node_values.front() != '[') {
      preset_jsons.push_back(preset);
      continue;
    }

    std::vector<std::string> kept_values;
    for (const auto& node_value : jsonObjectArrayItems(node_values)) {
      const std::string pipeline_id = simpleJsonStringField(node_value, "pipeline_id");
      const std::string node_id = simpleJsonStringField(node_value, "node_id");
      if (!pipeline_id.empty() && !node_id.empty() &&
          generated_node_keys.count(pipeline_id + "." + node_id)) {
        kept_values.push_back(node_value);
      }
    }

    std::ostringstream values;
    values << "[";
    for (size_t i = 0; i < kept_values.size(); ++i) {
      if (i) values << ",";
      values << kept_values[i];
    }
    values << "]";
    preset_jsons.push_back(upsertObjectMember(preset, "node_values", values.str()));
  }

  std::ostringstream next_presets;
  next_presets << "[";
  for (size_t i = 0; i < preset_jsons.size(); ++i) {
    if (i) next_presets << ",";
    next_presets << preset_jsons[i];
  }
  next_presets << "]";
  return upsertObjectMember(workspace_json, "presets", next_presets.str());
}

std::string mergeSnapshotWithRegeneratedProject(const std::string& workspace_json,
                                                const std::string& snapshot_json,
                                                const RegeneratedProject& regenerated) {
  std::string out = upsertObjectMember(workspace_json, "pipelines", regenerated.pipelines_json);
  out = upsertObjectMember(out, "frontend_connections", regenerated.frontend_connections_json);
  return mergeSnapshot(out, snapshot_json);
}

std::string jsonArrayFromObjects(const std::vector<std::string>& objects) {
  std::ostringstream os;
  os << "[";
  for (size_t i = 0; i < objects.size(); ++i) {
    if (i) os << ",";
    os << objects[i];
  }
  os << "]";
  return os.str();
}

std::string singlePipelineObjectFromArray(const std::string& pipelines_json) {
  const auto objects = jsonObjectArrayItems(pipelines_json);
  return objects.size() == 1 ? objects.front() : std::string();
}

std::string pipelineIdentity(const std::string& pipeline_object) {
  const std::string pipe_id = simpleJsonStringField(pipeline_object, "pipe_id");
  if (!pipe_id.empty()) return pipe_id;
  return simpleJsonStringField(pipeline_object, "name", "GUI_PIPE_1");
}

std::string upsertPipelineObject(const std::string& workspace_json, const std::string& pipeline_object) {
  const std::string target_pipe_id = simpleJsonStringField(pipeline_object, "pipe_id");
  const std::string target_name = simpleJsonStringField(pipeline_object, "name");
  std::vector<std::string> pipelines;
  bool replaced = false;
  for (const auto& existing : jsonObjectArrayItems(topLevelJsonMemberValue(workspace_json, "pipelines"))) {
    const std::string existing_pipe_id = simpleJsonStringField(existing, "pipe_id");
    const std::string existing_name = simpleJsonStringField(existing, "name");
    const bool same_id = !target_pipe_id.empty() && existing_pipe_id == target_pipe_id;
    const bool same_name = target_pipe_id.empty() && !target_name.empty() && existing_name == target_name;
    if (same_id || same_name) {
      pipelines.push_back(pipeline_object);
      replaced = true;
    } else {
      pipelines.push_back(existing);
    }
  }
  if (!replaced) pipelines.push_back(pipeline_object);
  return upsertObjectMember(workspace_json, "pipelines", jsonArrayFromObjects(pipelines));
}

std::string buildExtraFields(const RegeneratedProject& regenerated, uint64_t workspace_revision) {
  std::ostringstream os;
  os << "\"updated_pipelines\":" << regenerated.pipelines_json
     << ",\"updated_frontend_connections\":" << regenerated.frontend_connections_json
     << ",\"pipeline_ids\":[";
  for (size_t i = 0; i < regenerated.pipeline_ids.size(); ++i) {
    if (i) os << ",";
    os << "\"" << jsonEscape(regenerated.pipeline_ids[i]) << "\"";
  }
  os << "],\"workspace_revision\":" << workspace_revision;
  return os.str();
}

std::string validationPipelineSelector(const std::string& request_json,
                                       const std::string& snapshot_json,
                                       const std::vector<std::string>& pipeline_ids) {
  if (pipeline_ids.empty()) return {};

  const auto groups = snapshotGroups(snapshot_json);
  auto pipeline_from_group = [&](const std::string& group_id) {
    if (group_id.empty() || group_id == "ALL") return std::string();
    for (const auto& group : groups) {
      if (group.id != group_id && group.pipeline_id != group_id) continue;
      if (group.origin_pipeline_ids.size() == 1) return group.origin_pipeline_ids.front();
      if (!group.pipeline_id.empty() && group.pipeline_id != "ALL") return group.pipeline_id;
    }
    return group_id;
  };
  auto selector_for_pipeline = [&](const std::string& pipe_id) {
    if (pipe_id.empty() || pipe_id == "ALL") return std::string();
    for (size_t i = 0; i < pipeline_ids.size(); ++i) {
      if (pipeline_ids[i] == pipe_id) return std::to_string(i + 1);
    }
    return std::string();
  };

  const std::string group_id = simpleJsonStringField(
      request_json, "group_id", simpleJsonStringField(snapshot_json, "group_id"));
  if (isAllPipelinesBuildRequest(request_json, snapshot_json)) return {};
  std::string selector = selector_for_pipeline(pipeline_from_group(group_id));
  if (!selector.empty()) return selector;

  const std::string active_pipeline = simpleJsonStringField(
      request_json, "active_pipeline", simpleJsonStringField(snapshot_json, "active_pipeline"));
  selector = selector_for_pipeline(active_pipeline);
  if (!selector.empty()) return selector;

  return "1";
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

std::string base64Encode(const std::vector<uint8_t>& data) {
  static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((data.size() + 2) / 3) * 4);
  for (size_t i = 0; i < data.size(); i += 3) {
    const uint32_t b0 = data[i];
    const uint32_t b1 = (i + 1 < data.size()) ? data[i + 1] : 0;
    const uint32_t b2 = (i + 2 < data.size()) ? data[i + 2] : 0;
    const uint32_t n = (b0 << 16) | (b1 << 8) | b2;
    out.push_back(table[(n >> 18) & 0x3f]);
    out.push_back(table[(n >> 12) & 0x3f]);
    out.push_back(i + 1 < data.size() ? table[(n >> 6) & 0x3f] : '=');
    out.push_back(i + 2 < data.size() ? table[n & 0x3f] : '=');
  }
  return out;
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

std::string absolutePathFromRoot(const std::string& root_dir, const std::string& path) {
  if (path.empty()) return {};
  std::error_code ec;
  fs::path resolved(path);
  if (!resolved.is_absolute()) resolved = fs::path(root_dir) / resolved;
  const fs::path absolute = fs::absolute(resolved, ec);
  if (!ec) resolved = absolute;
  return resolved.lexically_normal().string();
}

BackendRuntimeConfig normalizedBackendConfig(const std::string& root_dir,
                                             BackendRuntimeConfig config) {
  config.as_server_path = absolutePathFromRoot(root_dir, config.as_server_path);
  config.alsatplg_path = absolutePathFromRoot(root_dir, config.alsatplg_path);
  config.helper_script_path = absolutePathFromRoot(root_dir, config.helper_script_path);
  config.as_log_path = absolutePathFromRoot(root_dir, config.as_log_path);
  config.trace_ldc_path = absolutePathFromRoot(root_dir, config.trace_ldc_path);
  config.datalink_endpoint = absolutePathFromRoot(root_dir, config.datalink_endpoint);
  config.as_server_rpc_mode = lowerAscii(config.as_server_rpc_mode);
  if (config.as_server_rpc_mode != "socket") config.as_server_rpc_mode = "once";
  if (config.as_server_host.empty()) config.as_server_host = config.as_server_host;
  if (config.as_server_port == 0) config.as_server_port = config.as_server_port;
  if (config.helper_python.empty()) config.helper_python = "python3";
  if (config.ready_timeout_ms <= 0) config.ready_timeout_ms = 120000;
  return config;
}

std::string runtimeAudioDriverFactory(const std::string& request_json,
                                      const BackendRuntimeConfig& config) {
  const std::string explicit_driver = simpleJsonStringField(request_json, "driver_factory", "");
  if (!explicit_driver.empty()) return explicit_driver;
  return config.runtime_audio_driver_factory;
}

std::string workspaceResponse(const std::string& workspace_id,
                              const std::string& input_path,
                              const std::string& source_path,
                              const std::string& workspace_json,
                              uint64_t workspace_revision) {
  const std::string fields =
      "\"workspace_id\":\"" + jsonEscape(workspace_id) + "\","
      "\"workspace_path\":\"" + jsonEscape(input_path) + "\","
      "\"source_path\":\"" + jsonEscape(source_path) + "\","
      "\"workspace_revision\":" + std::to_string(workspace_revision);
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
  return upsertObjectMember(workspace_json, "audio_studio_gui", fields.str());
}

std::string failureResponse(const std::string& stage,
                            const std::string& message,
                            const std::string& request_json,
                            const std::vector<std::string>& details = {},
                            const std::string& extra_fields = {}) {
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
  if (!extra_fields.empty()) os << "," << extra_fields;
  os << "}";
  return os.str();
}

class SocketConfigCompileClient final : public IConfigCompileClient {
public:
  GuiConfigCompileResult compile(const GuiConfigCompileRequest& request) override;
};

class ProcessValidationRunner final : public IValidationRunner {
public:
  ~ProcessValidationRunner() override;
  ValidationResult start(const ValidationRequest& request) override;
  ValidationResult waitReady(const ValidationRequest& request) override;
  ValidationResult stop(const std::string& workspace_id) override;

private:
#if !defined(_WIN32)
  std::mutex mutex_;
  std::map<std::string, pid_t> sessions_;
#endif
};

ProcessValidationRunner::~ProcessValidationRunner() {
#if !defined(_WIN32)
  std::vector<std::string> workspace_ids;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    for (const auto& session : sessions_) workspace_ids.push_back(session.first);
  }
  for (const auto& workspace_id : workspace_ids) stop(workspace_id);
#endif
}

#if !defined(_WIN32)
bool writeAllToSocket(int fd, const std::string& data, std::string& error) {
  size_t written = 0;
  while (written < data.size()) {
    const ssize_t n = send(fd, data.data() + written, data.size() - written,
#ifdef MSG_NOSIGNAL
                           MSG_NOSIGNAL
#else
                           0
#endif
    );
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

GuiConfigCompileResult parseConfigCompileResponse(const std::string& response) {
  GuiConfigCompileResult result;
  const size_t json_begin = response.find('{');
  if (json_begin == std::string::npos) {
    result.ok = false;
    result.message = "invalid as_server config.compile response: missing JSON object";
    if (!response.empty()) result.diagnostics.push_back(response);
    return result;
  }
  const std::string json_response = response.substr(json_begin);
  const std::string error_json = jsonMemberValue(json_response, "error");
  if (!error_json.empty()) {
    result.ok = false;
    result.message = simpleJsonStringField(error_json, "message", "as_server config.compile returned error");
    result.diagnostics.push_back(error_json);
    return result;
  }

  const std::string result_json = jsonMemberValue(json_response, "result");
  if (result_json.empty()) {
    result.ok = false;
    result.message = "invalid as_server config.compile response: missing result";
    result.diagnostics.push_back(json_response);
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
}

#if !defined(_WIN32)
bool runConfigCompileRpcOnce(const std::string& as_server,
                             const std::string& working_dir,
                             const std::string& rpc_request,
                             std::string& response,
                             std::string& error) {
  if (as_server.empty() || !fs::exists(as_server)) {
    error = as_server.empty() ? "as_server --rpc-once executable not found"
                              : "as_server --rpc-once executable not found: " + as_server;
    return false;
  }
  const std::string cmd = (working_dir.empty() ? std::string() : ("cd " + shellQuote(working_dir) + " && ")) +
                          shellQuote(as_server) + " --rpc-once " + shellQuote(rpc_request) + " 2>&1";
  FILE* pipe = ::popen(cmd.c_str(), "r");
  if (!pipe) {
    error = std::string("failed to start as_server --rpc-once: ") + std::strerror(errno);
    return false;
  }
  char buffer[4096];
  while (std::fgets(buffer, sizeof(buffer), pipe)) response += buffer;
  const int status = ::pclose(pipe);
  if (status != 0) {
    error = response.empty() ? "as_server --rpc-once failed" : response;
    return false;
  }
  if (response.empty()) {
    error = "as_server --rpc-once returned no response";
    return false;
  }
  return true;
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

  const bool prefer_socket = lowerAscii(request.as_server_rpc_mode) == "socket";
  std::string rpc_once_response;
  std::string rpc_once_error;
  if (!prefer_socket &&
      runConfigCompileRpcOnce(request.as_server, request.working_dir, rpc_request,
                              rpc_once_response, rpc_once_error)) {
    return parseConfigCompileResponse(rpc_once_response);
  }

  std::string error;
  const int fd = connectTcp(request.as_server_host, request.as_server_port,
                            request.as_server_timeout_ms, error);
  if (fd < 0) {
    const std::string socket_error = error.empty() ? "failed to connect as_server config.compile RPC" : error;
    result.ok = false;
    result.message = prefer_socket ? socket_error : (socket_error + "; " + rpc_once_error);
    if (!rpc_once_response.empty()) result.diagnostics.push_back(rpc_once_response);
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

  return parseConfigCompileResponse(response);
#endif
}

#if !defined(_WIN32)
bool waitForProcessExit(pid_t pid, int timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  int status = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    const pid_t done = ::waitpid(pid, &status, WNOHANG);
    if (done == pid || (done < 0 && errno == ECHILD)) return true;
    ::usleep(100000);
  }
  return false;
}

void stopProcessGroup(pid_t pid) {
  if (pid <= 0) return;
  if (::kill(-pid, SIGINT) == 0 && waitForProcessExit(pid, 5000)) return;
  if (::kill(-pid, SIGTERM) == 0 && waitForProcessExit(pid, 2000)) return;
  if (::kill(-pid, SIGKILL) == 0) waitForProcessExit(pid, 2000);
}
#endif

ValidationResult ProcessValidationRunner::start(const ValidationRequest& request) {
  ValidationResult result;
  if (request.script_path.empty()) {
    result.ok = false;
    result.message = "validation script path is not configured";
    return result;
  }
  if (!fs::exists(request.script_path)) {
    result.ok = false;
    result.message = "validation script not found: " + request.script_path;
    return result;
  }

#if defined(_WIN32)
  result.ok = false;
  result.message = "validation sessions are not implemented on Windows GUI backend yet";
  return result;
#else
  stop(request.workspace_id);

  const std::string log_path = (fs::path(request.workspace_dir) / "audio_studio_validation.log").string();
  const std::string ready_path = (fs::path(request.workspace_dir) / "audio_studio_validation.ready").string();
  const std::string as_server_ready_path = (fs::path(request.workspace_dir) / "audio_studio_as_server.ready").string();
  std::error_code ec;
  fs::remove(ready_path, ec);
  fs::remove(as_server_ready_path, ec);
  fs::remove(request.test_list_path, ec);
  const fs::path test_dir = fs::path(request.test_list_path).parent_path();
  const fs::path default_capture_input =
      fs::path(request.script_path).parent_path().parent_path().parent_path() /
      "Misc/sof_test/streams/wav/thetest48.wav";
  if (fs::exists(default_capture_input)) {
    fs::create_directories(test_dir, ec);
    fs::copy_file(default_capture_input, test_dir / "sof_fileio_in.wav",
                  fs::copy_options::overwrite_existing, ec);
  }
  const std::string cmd =
      shellQuote(request.python.empty() ? std::string("python3") : request.python) +
      " " + shellQuote(request.script_path) +
      " --audio-controller-log --as-server-host " + shellQuote(request.as_server_host) +
      " --as-server-port " + std::to_string(request.as_server_port) +
      (request.as_server_path.empty() ? "" : " --as-server " + shellQuote(request.as_server_path)) +
      (request.as_log_path.empty() ? "" : " --as-log " + shellQuote(request.as_log_path)) +
      (request.trace_ldc_path.empty() ? "" : " --trace-ldc " + shellQuote(request.trace_ldc_path)) +
      (request.datalink_endpoint.empty() ? "" : " --datalink-endpoint " + shellQuote(request.datalink_endpoint)) +
      (request.qemu_gdb_port == 0 ? "" : " --qemu-gdb-port " + std::to_string(request.qemu_gdb_port)) +
      (request.qemu_gdb_wait ? " --qemu-gdb-wait" : "") +
      " --gui-keep-alive --gui-ready-marker " + shellQuote(ready_path) +
      " --gui-as-server-ready-marker " + shellQuote(as_server_ready_path) +
      " --gui-test-list " + shellQuote(request.test_list_path) +
      " --gui-ready-after-pipeinstall" +
      " > " + shellQuote(log_path) + " 2>&1";

  const pid_t pid = ::fork();
  if (pid < 0) {
    result.ok = false;
    result.message = std::string("failed to fork validation session: ") + std::strerror(errno);
    return result;
  }
  if (pid == 0) {
    ::setsid();
    ::execl("/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }

  const long ready_timeout_ms = request.ready_timeout_ms > 0 ? request.ready_timeout_ms : 120000;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ready_timeout_ms);
  int status = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    if (fs::exists(as_server_ready_path)) {
      std::lock_guard<std::mutex> lk(mutex_);
      sessions_[request.workspace_id] = pid;
      result.ok = true;
      result.message = "validation as_server ready";
      result.diagnostics.push_back("validation_log=" + log_path);
      result.diagnostics.push_back("as_server_ready_file=" + as_server_ready_path);
      result.diagnostics.push_back("ready_file=" + ready_path);
      result.diagnostics.push_back("test_list=" + request.test_list_path);
      return result;
    }
    const pid_t done = ::waitpid(pid, &status, WNOHANG);
    if (done == pid) {
      result.ok = false;
      if (WIFEXITED(status)) {
        result.message = "validation failed with exit code " + std::to_string(WEXITSTATUS(status));
      } else if (WIFSIGNALED(status)) {
        result.message = "validation killed by signal " + std::to_string(WTERMSIG(status));
      } else {
        result.message = "validation failed before ready";
      }
      result.diagnostics.push_back("validation_log=" + log_path);
      return result;
    }
    ::usleep(100000);
  }

  stopProcessGroup(pid);
  result.ok = false;
  result.message = "validation as_server did not become ready before timeout";
  result.diagnostics.push_back("validation_log=" + log_path);
  result.diagnostics.push_back("as_server_ready_file=" + as_server_ready_path);
  result.diagnostics.push_back("ready_file=" + ready_path);
  return result;
#endif
}

ValidationResult ProcessValidationRunner::waitReady(const ValidationRequest& request) {
  ValidationResult result;
#if defined(_WIN32)
  (void)request;
  result.ok = false;
  result.message = "validation sessions are not implemented on Windows GUI backend yet";
  return result;
#else
  const std::string log_path = (fs::path(request.workspace_dir) / "audio_studio_validation.log").string();
  const std::string ready_path = (fs::path(request.workspace_dir) / "audio_studio_validation.ready").string();
  pid_t pid = 0;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = sessions_.find(request.workspace_id);
    if (it == sessions_.end()) {
      result.ok = false;
      result.message = "validation session is not running";
      return result;
    }
    pid = it->second;
  }

  const long ready_timeout_ms = request.ready_timeout_ms > 0 ? request.ready_timeout_ms : 120000;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ready_timeout_ms);
  int status = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    if (fs::exists(ready_path)) {
      result.ok = true;
      result.message = "validation session ready";
      result.diagnostics.push_back("validation_log=" + log_path);
      result.diagnostics.push_back("ready_file=" + ready_path);
      return result;
    }
    const pid_t done = ::waitpid(pid, &status, WNOHANG);
    if (done == pid) {
      {
        std::lock_guard<std::mutex> lk(mutex_);
        sessions_.erase(request.workspace_id);
      }
      result.ok = false;
      if (WIFEXITED(status)) {
        result.message = "validation failed with exit code " + std::to_string(WEXITSTATUS(status));
      } else if (WIFSIGNALED(status)) {
        result.message = "validation killed by signal " + std::to_string(WTERMSIG(status));
      } else {
        result.message = "validation failed before ready";
      }
      result.diagnostics.push_back("validation_log=" + log_path);
      result.diagnostics.push_back("ready_file=" + ready_path);
      return result;
    }
    ::usleep(100000);
  }

  result.ok = false;
  result.message = "validation did not become ready before timeout";
  result.diagnostics.push_back("validation_log=" + log_path);
  result.diagnostics.push_back("ready_file=" + ready_path);
  return result;
#endif
}

ValidationResult ProcessValidationRunner::stop(const std::string& workspace_id) {
  ValidationResult result;
  result.ok = true;
  result.message = "validation session stopped";
#if !defined(_WIN32)
  pid_t pid = 0;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = sessions_.find(workspace_id);
    if (it == sessions_.end()) return result;
    pid = it->second;
    sessions_.erase(it);
  }
  stopProcessGroup(pid);
#else
  (void)workspace_id;
#endif
  return result;
}

} // namespace

BuildOrchestrator::BuildOrchestrator(std::string root_dir,
                                     std::shared_ptr<IConfigCompileClient> compile_client,
                                     std::shared_ptr<IValidationRunner> validation_runner,
                                     BackendRuntimeConfig config)
  : compile_client_(std::move(compile_client)),
    validation_runner_(std::move(validation_runner)) {
  std::error_code ec;
  const fs::path requested_root = root_dir.empty() ? fs::path(".") : fs::path(std::move(root_dir));
  const fs::path absolute_root = fs::absolute(requested_root, ec);
  root_dir_ = (ec ? requested_root : absolute_root).lexically_normal().string();
  config_ = normalizedBackendConfig(root_dir_, std::move(config));
  if (!compile_client_) compile_client_ = std::make_shared<SocketConfigCompileClient>();
  if (!validation_runner_) validation_runner_ = std::make_shared<ProcessValidationRunner>();
}

BuildOrchestrator::~BuildOrchestrator() {
  std::vector<std::string> workspace_ids;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    for (const auto& workspace : workspaces_) workspace_ids.push_back(workspace.first);
  }
  for (const auto& workspace_id : workspace_ids) validation_runner_->stop(workspace_id);
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
  record.input_path = (fs::path(record.workspace_dir) /
                       (record.project_name + "_pipeline_all.json")).string();
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
          workspaceResponse(record.workspace_id, record.input_path, record.source_path, body,
                            record.workspace_revision)};
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
  const std::string all_json = readTextFile(record.input_path);
  if (all_json.empty()) {
    return {404, "application/json; charset=utf-8",
            failureResponse("open", "workspace project JSON is missing", request_json)};
  }
  const std::string source_json = readTextFile(record.source_path);
  const std::string regeneration_base_json = workspaceJsonWithSourceBaseline(all_json, source_json);
  validation_runner_->stop(record.workspace_id);
  {
    std::lock_guard<std::mutex> lk(mutex_);
    auto* found = findWorkspaceLocked(record.workspace_id);
    if (found) {
      found->loaded_pipeline_ids.clear();
      found->validated_pipeline_ids.clear();
      found->build_ok = false;
      record = *found;
    }
  }

  const std::string snapshot_json = snapshot.empty() ? request_json : snapshot;
  RegeneratedProject regenerated;
  std::vector<std::string> workspace_diagnostics;
  if (!regenerateProjectFromSnapshot(regeneration_base_json, snapshot_json, regenerated, workspace_diagnostics)) {
    return {200, "application/json; charset=utf-8",
            failureResponse("workspace",
                            workspace_diagnostics.empty() ? "failed to regenerate workspace pipelines" : workspace_diagnostics.front(),
                            request_json,
                            workspace_diagnostics)};
  }
  if (isAllPipelinesBuildRequest(request_json, snapshot_json)) {
    mergeSourcePipelinesForAllBuild(regeneration_base_json, regenerated);
  }
  uint64_t workspace_revision = record.workspace_revision;

  const std::string all_json_next = mergeSnapshotWithRegeneratedProject(
      filterPresetNodeValues(all_json, regenerated.generated_node_keys),
      snapshot_json,
      regenerated);
  if (!writeTextFile(record.input_path, all_json_next)) {
    return {500, "application/json; charset=utf-8",
            failureResponse("workspace", "failed to update workspace JSON", request_json)};
  }

  {
    std::lock_guard<std::mutex> lk(mutex_);
    auto* found = findWorkspaceLocked(record.workspace_id);
    if (found) {
      found->build_ok = false;
      found->workspace_revision += 1;
      workspace_revision = found->workspace_revision;
      record = *found;
    }
  }

  fs::create_directories(record.output_dir);
  const std::string test_list_path = (fs::path(record.output_dir) / "audio_studio_test_list.txt").string();
  const std::string validation_pipeline = validationPipelineSelector(request_json, snapshot_json, regenerated.pipeline_ids);
  const std::string helper_datalink = config_.datalink_endpoint.empty()
                                          ? "as_datalink"
                                          : config_.datalink_endpoint;

  ValidationRequest validation_request;
  validation_request.workspace_id = record.workspace_id;
  validation_request.workspace_dir = record.workspace_dir;
  validation_request.project_name = record.project_name;
  validation_request.test_list_path = test_list_path;
  validation_request.python = config_.helper_python;
  validation_request.script_path = config_.helper_script_path;
  validation_request.as_server_path = config_.as_server_path;
  validation_request.as_log_path = config_.as_log_path;
  validation_request.trace_ldc_path = config_.trace_ldc_path;
  validation_request.as_server_host = config_.as_server_host;
  validation_request.as_server_port = config_.as_server_port;
  validation_request.ready_timeout_ms = config_.ready_timeout_ms;
  validation_request.datalink_endpoint = config_.datalink_endpoint;
  validation_request.qemu_gdb_port = config_.qemu_gdb_port;
  validation_request.qemu_gdb_wait = config_.qemu_gdb_wait;

  auto validation_start = validation_runner_->start(validation_request);
  if (!validation_start.ok) {
    validation_runner_->stop(record.workspace_id);
    return {200, "application/json; charset=utf-8",
            failureResponse("validation", validation_start.message, request_json, validation_start.diagnostics,
                            buildExtraFields(regenerated, workspace_revision))};
  }

  GuiConfigCompileRequest compile_request;
  compile_request.input_path = record.input_path;
  compile_request.output_dir = record.output_dir;
  compile_request.project_name = record.project_name;
  compile_request.working_dir = root_dir_;
  compile_request.alsatplg = config_.alsatplg_path;
  compile_request.as_server = config_.as_server_path;
  compile_request.as_server_rpc_mode = config_.as_server_rpc_mode;
  compile_request.as_server_host = config_.as_server_host;
  compile_request.as_server_port = config_.as_server_port;
  compile_request.as_server_timeout_ms = config_.as_server_timeout_ms;
  compile_request.build_tplg = true;
  compile_request.strict = true;
  compile_request.plugin_paths = {};

  auto compile_result = compile_client_->compile(compile_request);
  if (!compile_result.ok) {
    validation_runner_->stop(record.workspace_id);
    return {200, "application/json; charset=utf-8",
            failureResponse("compile", compile_result.message, request_json, compile_result.diagnostics,
                            buildExtraFields(regenerated, workspace_revision))};
  }

  validation_request.tplg_path = compile_result.tplg_path;
  const std::string test_list =
      "ac_run --endpoint " + shellQuote(helper_datalink) + " --mtu 512\n"
      "trace on\n"
      "pipeinstall " + (validation_pipeline.empty() ? std::string() : ("-p " + validation_pipeline + " ")) +
      compile_result.tplg_path + "\n"
      "sleep 3600\n";
  if (!writeTextFile(test_list_path, test_list)) {
    validation_runner_->stop(record.workspace_id);
    return {500, "application/json; charset=utf-8",
            failureResponse("validation", "failed to write audio_studio_test_list", request_json)};
  }

  auto validation_result = validation_runner_->waitReady(validation_request);
  if (!validation_result.ok) {
    validation_runner_->stop(record.workspace_id);
    return {200, "application/json; charset=utf-8",
            failureResponse("validation", validation_result.message, request_json, validation_result.diagnostics,
                            buildExtraFields(regenerated, workspace_revision))};
  }

  {
    std::lock_guard<std::mutex> lk(mutex_);
    auto* found = findWorkspaceLocked(record.workspace_id);
    if (found) {
      found->validated_pipeline_ids = std::set<std::string>(regenerated.pipeline_ids.begin(), regenerated.pipeline_ids.end());
      found->loaded_pipeline_ids = found->validated_pipeline_ids;
      found->build_ok = !found->validated_pipeline_ids.empty();
      workspace_revision = found->workspace_revision;
    }
  }

  std::ostringstream os;
  os << "{\"ok\":true,\"status\":\"PIPE_LOADED\",\"runtime_state\":\"PIPE_LOADED\""
     << ",\"workspace_id\":\"" << jsonEscape(record.workspace_id) << "\""
     << ",\"input_path\":\"" << jsonEscape(record.input_path) << "\""
     << ",\"tplg_path\":\"" << jsonEscape(compile_result.tplg_path) << "\""
     << ",\"test_list_path\":\"" << jsonEscape(test_list_path) << "\""
     << ",\"validation\":\"" << jsonEscape(validation_result.message) << "\""
     << "," << buildExtraFields(regenerated, workspace_revision)
     << "}";
  return {200, "application/json; charset=utf-8", os.str()};
}

HttpResponse BuildOrchestrator::unloadPipeline(const std::string& request_json) {
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

  auto stop_result = validation_runner_->stop(record.workspace_id);
  if (!stop_result.ok) {
    return {200, "application/json; charset=utf-8",
            failureResponse("runtime", stop_result.message, request_json, stop_result.diagnostics)};
  }

  uint64_t workspace_revision = record.workspace_revision;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    auto* found = findWorkspaceLocked(record.workspace_id);
    if (found) {
      found->loaded_pipeline_ids.clear();
      found->validated_pipeline_ids.clear();
      workspace_revision = found->workspace_revision;
    }
  }

  std::ostringstream os;
  os << "{\"ok\":true,\"status\":\"PIPE_UNLOADED\",\"runtime_state\":\"PIPE_UNLOADED\""
     << ",\"workspace_id\":\"" << jsonEscape(record.workspace_id) << "\""
     << ",\"workspace_revision\":" << workspace_revision << "}";
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
  const std::string source_json = stripGuiOnlySectionsForSave(workspace_json);
  if (workspace_json.empty() || source_json.empty() || !writeTextFile(record.source_path, source_json)) {
    return {500, "application/json; charset=utf-8",
            "{\"ok\":false,\"status\":\"failed\",\"stage\":\"save\",\"error\":\"write_failed\",\"diagnostics\":[\"failed to write source config JSON\"]}"};
  }
  return {200, "application/json; charset=utf-8",
          "{\"ok\":true,\"status\":\"saved\",\"source_path\":\"" + jsonEscape(record.source_path) + "\"}"};
}

HttpResponse BuildOrchestrator::stageDaiInput(const std::string& workspace_id,
                                              uint32_t dai_index,
                                              const std::string& file_name,
                                              const std::string& wav_bytes) {
  WorkspaceRecord record;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    auto* found = findWorkspaceLocked(workspace_id);
    if (!found || !found->build_ok) {
      return {409, "application/json; charset=utf-8",
              R"({"ok":false,"error":"build_required","diagnostics":["workspace must be built before staging FILE_IO DAI input"]})"};
    }
    record = *found;
  }

  if (wav_bytes.size() < 12u || wav_bytes.compare(0u, 4u, "RIFF") != 0 ||
      wav_bytes.compare(8u, 4u, "WAVE") != 0) {
    return {400, "application/json; charset=utf-8",
            R"({"ok":false,"error":"invalid_wav","diagnostics":["FILE_IO DAI input must be a RIFF/WAVE file"]})"};
  }

  const std::string path = fileIoDaiPath(record.output_dir, "sof_fileio_in", dai_index);
  if (!writeTextFile(path, wav_bytes)) {
    return {500, "application/json; charset=utf-8",
            R"({"ok":false,"error":"write_failed","diagnostics":["failed to stage FILE_IO DAI input"]})"};
  }

  std::ostringstream os;
  os << "{\"ok\":true,\"accepted_bytes\":" << wav_bytes.size()
     << ",\"file_name\":\"" << jsonEscape(file_name) << "\""
     << ",\"stream_uri\":\"audio-studio://workspace/" << jsonEscape(workspace_id)
     << "/file-io-dai/" << dai_index << "/input\"}";
  return {200, "application/json; charset=utf-8", os.str()};
}

HttpResponse BuildOrchestrator::fetchDaiOutput(const std::string& workspace_id,
                                               uint32_t dai_index,
                                               const std::string& file_name) {
  WorkspaceRecord record;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    auto* found = findWorkspaceLocked(workspace_id);
    if (!found || !found->build_ok) {
      return {409, "application/json; charset=utf-8",
              R"({"ok":false,"error":"build_required","diagnostics":["workspace must be built before reading FILE_IO DAI output"]})"};
    }
    record = *found;
  }

  const std::string path = fileIoDaiPath(record.output_dir, "sof_fileio_out", dai_index);
  const std::string wav_bytes = readTextFile(path);
  if (wav_bytes.size() < 12u || wav_bytes.compare(0u, 4u, "RIFF") != 0 ||
      wav_bytes.compare(8u, 4u, "WAVE") != 0) {
    return {404, "application/json; charset=utf-8",
            "{\"ok\":false,\"error\":\"output_not_ready\",\"file_name\":\"" +
                jsonEscape(file_name) + "\"}"};
  }
  return {200, "audio/wav", wav_bytes};
}

namespace {

std::string jsonStringMember(const audio_studio::rpc::JsonValue& value,
                             const std::string& field,
                             const std::string& fallback = {}) {
  if (!value.isObject() || !value.has(field)) return fallback;
  const auto& item = value.at(field);
  if (item.isString()) return item.asString();
  if (item.isNumber()) return item.dump();
  if (item.isBool()) return item.asBool() ? "true" : "false";
  return fallback;
}

std::string localNodeIdFromRuntimeParamRequest(const audio_studio::rpc::JsonValue& request) {
  const std::string explicit_id = jsonStringMember(request, "pipeline_node_id");
  if (!explicit_id.empty()) return explicit_id;
  std::string node_id = jsonStringMember(request, "node_id");
  const std::string marker = "__";
  const size_t marker_pos = node_id.find(marker);
  if (marker_pos != std::string::npos) node_id = node_id.substr(marker_pos + marker.size());
  return node_id;
}

} // namespace

HttpResponse BuildOrchestrator::updateRuntimeParameter(const std::string& request_json) {
  audio_studio::rpc::JsonValue request;
  try {
    request = audio_studio::rpc::parseJson(request_json.empty() ? "{}" : request_json);
  } catch (const std::exception& e) {
    return {400, "application/json; charset=utf-8",
            "{\"ok\":false,\"error\":\"invalid_json\",\"diagnostics\":[\"" + jsonEscape(e.what()) + "\"]}"};
  }

  const std::string pipeline_id = jsonStringMember(request, "pipeline_id");
  const std::string node_id = localNodeIdFromRuntimeParamRequest(request);
  const std::string param_id = jsonStringMember(request, "param_id");
  if (pipeline_id.empty() || node_id.empty() || param_id.empty() || !request.has("value")) {
    return {400, "application/json; charset=utf-8",
            "{\"ok\":false,\"error\":\"missing_fields\",\"diagnostics\":[\"pipeline_id, node_id, param_id and value are required\"]}"};
  }

  WorkspaceRecord record;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    std::string workspace_id = jsonStringMember(request, "workspace_id");
    if (workspace_id.empty()) {
      auto& opened = openProjectLocked(jsonStringMember(request, "project", "a2/A2.json"));
      workspace_id = opened.workspace_id;
    }
    auto* found = findWorkspaceLocked(workspace_id);
    if (!found) {
      return {404, "application/json; charset=utf-8",
              "{\"ok\":false,\"error\":\"workspace_not_found\"}"};
    }
    record = *found;
  }

  const std::string workspace_json = readTextFile(record.input_path);
  if (workspace_json.empty()) {
    return {404, "application/json; charset=utf-8",
            "{\"ok\":false,\"error\":\"workspace_json_missing\"}"};
  }

  audio_studio::rpc::JsonValue project;
  try {
    project = audio_studio::rpc::parseJson(workspace_json);
  } catch (const std::exception& e) {
    return {500, "application/json; charset=utf-8",
            "{\"ok\":false,\"error\":\"workspace_json_invalid\",\"diagnostics\":[\"" + jsonEscape(e.what()) + "\"]}"};
  }

  auto& presets = project["presets"];
  if (!presets.isArray()) presets = audio_studio::rpc::JsonValue::array();
  auto& preset_array = presets.asArray();
  audio_studio::rpc::JsonValue* preset = nullptr;
  for (auto& item : preset_array) {
    if (item.isObject() && jsonStringMember(item, "preset_id") == "inspector_preset") {
      preset = &item;
      break;
    }
  }
  if (!preset) {
    audio_studio::rpc::JsonValue created = audio_studio::rpc::JsonValue::object();
    created["preset_id"] = "inspector_preset";
    created["description"] = "Frontend Inspector scratch values.";
    created["load_mode"] = "frontend_only";
    created["node_values"] = audio_studio::rpc::JsonValue::array();
    preset_array.push_back(std::move(created));
    preset = &preset_array.back();
  }

  auto& node_values = (*preset)["node_values"];
  if (!node_values.isArray()) node_values = audio_studio::rpc::JsonValue::array();
  auto& values_array = node_values.asArray();
  audio_studio::rpc::JsonValue* entry = nullptr;
  for (auto& item : values_array) {
    if (!item.isObject()) continue;
    if (jsonStringMember(item, "pipeline_id") == pipeline_id &&
        jsonStringMember(item, "node_id") == node_id) {
      entry = &item;
      break;
    }
  }
  if (!entry) {
    audio_studio::rpc::JsonValue created = audio_studio::rpc::JsonValue::object();
    created["pipeline_id"] = pipeline_id;
    created["node_id"] = node_id;
    created["values"] = audio_studio::rpc::JsonValue::object();
    values_array.push_back(std::move(created));
    entry = &values_array.back();
  }

  auto& values = (*entry)["values"];
  if (!values.isObject()) values = audio_studio::rpc::JsonValue::object();
  values[param_id] = request.at("value");

  if (!writeTextFile(record.input_path, project.dump())) {
    return {500, "application/json; charset=utf-8",
            "{\"ok\":false,\"error\":\"workspace_write_failed\"}"};
  }

  uint64_t workspace_revision = record.workspace_revision;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    auto* found = findWorkspaceLocked(record.workspace_id);
    if (found) {
      found->workspace_revision += 1;
      workspace_revision = found->workspace_revision;
    }
  }

  std::ostringstream os;
  os << "{\"ok\":true,\"preset_id\":\"inspector_preset\""
     << ",\"pipeline_id\":\"" << jsonEscape(pipeline_id) << "\""
     << ",\"node_id\":\"" << jsonEscape(node_id) << "\""
     << ",\"param_id\":\"" << jsonEscape(param_id) << "\""
     << ",\"runtime_state\":\"" << jsonEscape(jsonStringMember(request, "runtime_state")) << "\""
     << ",\"control_apply\":\"pending_as_control\""
     << ",\"workspace_id\":\"" << jsonEscape(record.workspace_id) << "\""
     << ",\"workspace_revision\":" << workspace_revision
     << "}";
  return {200, "application/json; charset=utf-8", os.str()};
}

struct GuiRuntimeEngine::PlaybackWorker {
  explicit PlaybackWorker(BackendRuntimeConfig config)
    : config_(std::move(config)) {}

  struct Frame {
    std::vector<uint8_t> bytes;
  };

  struct BufferProgress {
    int64_t last_progress_ms = 0;
    bool seen = false;
  };

  struct BlockageStatus {
    bool stalled = false;
    bool system_info = false;
    std::string gui_edge_key;
    std::string system_edge_key;
    uint64_t system_consumed_bytes = 0;
  };

  ~PlaybackWorker() {
    stop();
  }

  std::string start(const std::string& request_json) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (active_ && !eof_requested_ && rpc_ready_) return statusJsonLocked(true);
    }
    stop();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      active_ = true;
      stopping_ = false;
      eof_requested_ = false;
      in_flight_ = false;
      error_.clear();
      queued_bytes_ = 0;
      played_bytes_ = 0;
      last_dequeue_ms_ = nowMs();
      session_id_ = simpleJsonStringField(request_json, "session_id", "gui_playback");
      blocked_edge_key_ = simpleJsonStringField(request_json, "edge_key");
      next_push_ms_ = inferFrameDelayMs(request_json);
      sample_frame_bytes_ = inferSampleFrameBytes(request_json);
      sample_rate_ = inferSampleRate(request_json);
    }
    {
      std::lock_guard<std::mutex> lock(system_info_mutex_);
      system_buffer_progress_.clear();
    }

    setupRpc(request_json);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!rpc_ready_) {
        active_ = false;
        stopping_ = true;
        eof_requested_ = true;
        return statusJsonLocked(false);
      }
    }
    worker_ = std::thread([this] { run(); });
    return statusJson(true);
  }

  std::string stopJson() {
    stop();
    return "{\"ok\":true,\"playback\":{\"active\":false,\"queued_bytes\":0}}";
  }

  void stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
      active_ = false;
      eof_requested_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    cleanupRpc();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.clear();
      queued_bytes_ = 0;
      rpc_ready_ = false;
      in_flight_ = false;
    }
    {
      std::lock_guard<std::mutex> lock(system_info_mutex_);
      system_buffer_progress_.clear();
    }
  }

  std::string enqueueStream(const std::string& query, const std::string& frame) {
    const auto q = parseQuery(query);
    std::string edge_key;
    auto edge_it = q.find("edge_key");
    if (edge_it != q.end()) edge_key = edge_it->second;

    bool accepted = false;
    bool stalled = false;
    size_t queued = 0;
    std::string error;
    std::string blocked_edge;
    int64_t queue_age = 0;
    bool rpc_ready = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!edge_key.empty()) blocked_edge_key_ = edge_key;
      blocked_edge = edge_key.empty() ? blocked_edge_key_ : edge_key;
      const size_t aligned_bytes = alignedAudioBytes(frame.size(), sample_frame_bytes_);
      if (active_ && !eof_requested_ && aligned_bytes > 0 && queued_bytes_ + aligned_bytes <= kMaxQueueBytes) {
        Frame next;
        next.bytes.assign(frame.begin(), frame.begin() + static_cast<long>(aligned_bytes));
        queued_bytes_ += next.bytes.size();
        queue_.push_back(std::move(next));
        accepted = true;
      }
      queued = queued_bytes_;
      queue_age = nowMs() - last_dequeue_ms_;
      rpc_ready = rpc_ready_;
      error = error_;
    }
    cv_.notify_one();
    const auto blockage = blockageStatus(blocked_edge, queued, rpc_ready, queue_age, accepted);
    stalled = blockage.stalled;

    std::ostringstream os;
    os << "{\"ok\":" << (accepted ? "true" : "false")
       << ",\"stream\":true"
       << ",\"accepted\":" << (accepted ? "true" : "false")
       << ",\"accepted_bytes\":" << (accepted ? alignedAudioBytes(frame.size(), sample_frame_bytes_) : 0)
       << ",\"queued_bytes\":" << queued
       << ",\"queued_audio_ms\":" << queuedAudioMs(queued, sample_frame_bytes_, sample_rate_)
       << ",\"next_push_ms\":" << recommendedDelayMs(queued, sample_frame_bytes_, sample_rate_, next_push_ms_)
       << ",\"stalled\":" << (stalled ? "true" : "false")
       << ",\"blocked_edge_key\":\"" << jsonEscape(blockage.gui_edge_key) << "\""
       << ",\"blocked_system_edge_key\":\"" << jsonEscape(blockage.system_edge_key) << "\""
       << ",\"blocking_source\":\"" << (blockage.system_info ? "system_info" : (stalled ? "queue" : "none")) << "\""
       << ",\"blocked_consumed_bytes\":" << blockage.system_consumed_bytes;
    if (!error.empty()) os << ",\"error\":\"" << jsonEscape(error) << "\"";
    os << "}";
    return os.str();
  }

  std::string frameStatus(const std::string& query, const std::string& request_json) {
    const auto q = parseQuery(query);
    std::string edge_key = simpleJsonStringField(request_json, "edge_key");
    if (edge_key.empty()) {
      const auto edge_it = q.find("edge_key");
      if (edge_it != q.end()) edge_key = edge_it->second;
    }
    const int bytes_written = std::max(0, simpleJsonIntField(request_json, "bytes_written",
                                            simpleJsonIntField(request_json, "accepted_bytes", 0)));
    const int frame_bytes = std::max(bytes_written,
                                     simpleJsonIntField(request_json, "frame_bytes", bytes_written));
    const int frame_index = simpleJsonIntField(request_json, "frame_index", 0);
    const bool stream_ok = simpleJsonBoolField(request_json, "stream_ok", true);

    size_t queued = 0;
    bool active = false;
    bool stalled = false;
    std::string error;
    std::string blocked_edge;
    int64_t queue_age = 0;
    bool rpc_ready = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!edge_key.empty()) blocked_edge_key_ = edge_key;
      blocked_edge = edge_key.empty() ? blocked_edge_key_ : edge_key;
      queued = queued_bytes_;
      active = active_ && !eof_requested_;
      queue_age = nowMs() - last_dequeue_ms_;
      rpc_ready = rpc_ready_;
      error = error_;
    }
    const auto blockage = blockageStatus(blocked_edge, queued, rpc_ready, queue_age,
                                         stream_ok && bytes_written > 0);
    stalled = blockage.stalled;

    const bool accepted = active && stream_ok && bytes_written > 0 && error.empty();
    std::ostringstream os;
    os << "{\"ok\":" << (accepted ? "true" : "false")
       << ",\"frame\":true"
       << ",\"accepted\":" << (accepted ? "true" : "false")
       << ",\"frame_index\":" << frame_index
       << ",\"frame_bytes\":" << frame_bytes
       << ",\"accepted_bytes\":" << (accepted ? bytes_written : 0)
       << ",\"queued_bytes\":" << queued
       << ",\"queued_audio_ms\":" << queuedAudioMs(queued, sample_frame_bytes_, sample_rate_)
       << ",\"next_push_ms\":" << recommendedDelayMs(queued, sample_frame_bytes_, sample_rate_, next_push_ms_)
       << ",\"stalled\":" << (stalled ? "true" : "false")
       << ",\"blocked_edge_key\":\"" << jsonEscape(blockage.gui_edge_key) << "\""
       << ",\"blocked_system_edge_key\":\"" << jsonEscape(blockage.system_edge_key) << "\""
       << ",\"blocking_source\":\"" << (blockage.system_info ? "system_info" : (stalled ? "queue" : "none")) << "\""
       << ",\"blocked_consumed_bytes\":" << blockage.system_consumed_bytes;
    if (!error.empty()) os << ",\"error\":\"" << jsonEscape(error) << "\"";
    os << "}";
    return os.str();
  }

  std::string finish(const std::string& request_json) {
    const int timeout_ms = std::max(0, simpleJsonIntField(request_json, "timeout_ms", 5000));
    bool timeout = false;
    size_t queued = 0;
    uint64_t played = 0;
    std::string error;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      eof_requested_ = true;
      active_ = false;
      const auto ready = [&] { return !rpc_ready_ || (queue_.empty() && !in_flight_); };
      if (timeout_ms == 0) {
        timeout = !ready();
      } else if (!cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), ready)) {
        timeout = true;
      }
      queued = queued_bytes_;
      played = played_bytes_;
      if (timeout && error_.empty()) error_ = "playback EOS timed out waiting for queued frames to drain";
      error = error_;
    }

    if (!timeout) finishRpc();
    else cleanupRpc();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.clear();
      queued_bytes_ = 0;
      rpc_ready_ = false;
      in_flight_ = false;
      played = played_bytes_;
      error = error_;
    }

    const bool ok = error.empty() && !timeout;
    std::ostringstream os;
    os << "{\"ok\":" << (ok ? "true" : "false")
       << ",\"eos\":true,\"running\":false,\"runtime_state\":\"PIPE_LOADED\""
       << ",\"playback\":{\"active\":false,\"queued_bytes\":" << queued
       << ",\"played_bytes\":" << played << "}";
    if (!error.empty()) os << ",\"error\":\"" << jsonEscape(error) << "\"";
    os << "}";
    return os.str();
  }

private:
  static constexpr size_t kHighWaterBytes = 512 * 1024;
  static constexpr size_t kMaxQueueBytes = 4 * 1024 * 1024;

  static int64_t nowMs() {
    return static_cast<int64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
  }

  static int clampDelayMs(int value) {
    return std::max(1, std::min(200, value));
  }

  static int inferFrameDelayMs(const std::string& request_json) {
    const int explicit_ms = simpleJsonIntField(request_json, "frame_ms", 0);
    if (explicit_ms > 0) return clampDelayMs(explicit_ms);

    const int sample_rate = simpleJsonIntField(request_json, "sample_rate",
                            simpleJsonIntField(request_json, "sampleRate", 48000));
    const int frame_samples = simpleJsonIntField(request_json, "frame_samples",
                              simpleJsonIntField(request_json, "frameSamples", 0));
    if (sample_rate > 0 && frame_samples > 0) {
      return clampDelayMs(static_cast<int>(std::lround(
          1000.0 * static_cast<double>(frame_samples) / static_cast<double>(sample_rate))));
    }

    const int channels = std::max(1, simpleJsonIntField(request_json, "channels", 2));
    const int bits = simpleJsonIntField(request_json, "bits_per_sample",
                     simpleJsonIntField(request_json, "bits", 16));
    const int bytes_per_frame = std::max(1, channels * std::max(1, bits / 8));
    const int frame_bytes = simpleJsonIntField(request_json, "frame_bytes",
                            simpleJsonIntField(request_json, "frameBytes", 0));
    if (sample_rate > 0 && frame_bytes > 0) {
      const double samples = static_cast<double>(frame_bytes) / static_cast<double>(bytes_per_frame);
      return clampDelayMs(static_cast<int>(std::lround(1000.0 * samples / static_cast<double>(sample_rate))));
    }

    return 20;
  }

  static int inferSampleFrameBytes(const std::string& request_json) {
    const int channels = std::max(1, simpleJsonIntField(request_json, "channels", 2));
    const int bits = simpleJsonIntField(request_json, "bits_per_sample",
                     simpleJsonIntField(request_json, "bits", 16));
    return std::max(1, channels * std::max(1, bits / 8));
  }

  static int inferSampleRate(const std::string& request_json) {
    return std::max(1, simpleJsonIntField(request_json, "sample_rate",
                       simpleJsonIntField(request_json, "sampleRate", 48000)));
  }

  static size_t alignedAudioBytes(size_t bytes, int sample_frame_bytes) {
    const size_t frame = static_cast<size_t>(std::max(1, sample_frame_bytes));
    return bytes - (bytes % frame);
  }

  static int queuedAudioMs(size_t queued_bytes, int sample_frame_bytes, int sample_rate) {
    const double bytes_per_ms =
        std::max(1, sample_frame_bytes) * static_cast<double>(std::max(1, sample_rate)) / 1000.0;
    return static_cast<int>(std::lround(static_cast<double>(queued_bytes) / bytes_per_ms));
  }

  static int recommendedDelayMs(size_t queued_bytes,
                                int sample_frame_bytes,
                                int sample_rate,
                                int frame_delay_ms) {
    const int frame = clampDelayMs(frame_delay_ms);
    const int queued_ms = queuedAudioMs(queued_bytes, sample_frame_bytes, sample_rate);
    const int target_ms = std::max(40, frame * 2);
    if (queued_ms <= target_ms) return frame;
    return clampDelayMs(frame + queued_ms - target_ms);
  }

#if defined(AUDIO_STUDIO_GUI_PLAYBACK_RPC)
  static std::string jsonStringField(const audio_studio::rpc::JsonValue& value,
                                     const std::string& field,
                                     const std::string& fallback = {}) {
    if (!value.isObject() || !value.has(field)) return fallback;
    const auto& item = value.at(field);
    if (item.isString()) return item.asString();
    if (item.isNumber()) return std::to_string(item.asUInt64());
    return fallback;
  }

  static uint64_t jsonU64Field(const audio_studio::rpc::JsonValue& value,
                               const std::string& field,
                               uint64_t fallback = 0) {
    if (!value.isObject() || !value.has(field) || !value.at(field).isNumber()) return fallback;
    return value.at(field).asUInt64();
  }

  static bool jsonBoolField(const audio_studio::rpc::JsonValue& value,
                            const std::string& field,
                            bool fallback = false) {
    if (!value.isObject() || !value.has(field) || !value.at(field).isBool()) return fallback;
    return value.at(field).asBool();
  }
#endif

  BlockageStatus blockageStatus(const std::string& gui_edge_key,
                                size_t queued_bytes,
                                bool rpc_ready,
                                int64_t queue_age_ms,
                                bool expecting_system_progress) {
    BlockageStatus status;
    status.gui_edge_key = gui_edge_key;
    status.stalled = !rpc_ready || queued_bytes >= kHighWaterBytes ||
                     (queued_bytes > 0 && queue_age_ms >= 100);

#if defined(AUDIO_STUDIO_GUI_PLAYBACK_RPC)
    try {
      auto& drivers = audio_studio::drivers::DriverManager::instance();
      audio_studio::rpc::SocketJsonRpcTransport transport(drivers.socket(), system_info_endpoint_);
      audio_studio::rpc::JsonRpcClient client(transport);
      const auto result = client.call("systemInfo.buffers");
      if (!result.isObject() || !result.has("buffers") || !result.at("buffers").isArray()) return status;

      const int64_t now = nowMs();
      bool saw_buffer = false;
      bool found_blocked = false;
      BlockageStatus system_status;
      system_status.gui_edge_key = gui_edge_key;
      system_status.system_info = true;

      std::lock_guard<std::mutex> progress_lock(system_info_mutex_);
      for (const auto& buffer : result.at("buffers").asArray()) {
        if (!buffer.isObject()) continue;
        const std::string system_edge = jsonStringField(buffer, "edge_key");
        if (system_edge.empty()) continue;
        saw_buffer = true;

        const uint64_t avail = jsonU64Field(buffer, "avail_bytes", 0);
        const uint64_t produced = jsonU64Field(buffer, "produced_bytes", 0);
        const uint64_t consumed = jsonU64Field(buffer, "consumed_bytes", 0);
        const bool reported_stalled = jsonBoolField(buffer, "stalled", false);
        const bool has_pending_data = avail > 0 || produced > 0;
        auto& progress = system_buffer_progress_[system_edge];
        if (!progress.seen) {
          progress.seen = true;
          progress.last_progress_ms = now;
        }
        if (consumed > 0 || (!has_pending_data && !expecting_system_progress)) {
          progress.last_progress_ms = now;
          if (!reported_stalled) continue;
        }

        const bool no_consumption = (has_pending_data || expecting_system_progress) &&
                                    (now - progress.last_progress_ms) >= 100;
        if (reported_stalled || no_consumption) {
          system_status.stalled = true;
          system_status.system_edge_key = system_edge;
          system_status.system_consumed_bytes = consumed;
          found_blocked = true;
          break;
        }
      }

      if (saw_buffer) {
        if (found_blocked) return system_status;
        system_status.stalled = false;
        system_status.system_edge_key.clear();
        if (!status.stalled) return system_status;
      }
    } catch (...) {
    }
#endif
    return status;
  }

  void setupRpc(const std::string& request_json) {
#if defined(AUDIO_STUDIO_GUI_PLAYBACK_RPC)
    try {
      auto& drivers = audio_studio::drivers::DriverManager::instance();
      audio_studio::drivers::DriverManagerConfig driver_config;
      driver_config.enable_os = false;
      driver_config.enable_socket = true;
      driver_config.enable_filesystem = false;
      driver_config.enable_pipe = false;
      driver_config.enable_dynlib = false;
      driver_config.enable_datalink = false;
      driver_config.enable_audio = false;
      driver_config.enable_control = false;
      driver_config.enable_log = false;
      driver_config.enable_dump = false;
      auto status = drivers.initialize(driver_config);
      if (!status.ok()) throw std::runtime_error(status.message());

      audio_studio::rpc::SocketRpcEndpoint endpoint;
      endpoint.host = simpleJsonStringField(request_json, "as_server_host", config_.as_server_host);
      endpoint.port = static_cast<uint16_t>(simpleJsonIntField(
          request_json, "as_server_port", config_.as_server_port));
      endpoint.timeout_ms = 1000;
      system_info_endpoint_ = endpoint;
      system_info_endpoint_.timeout_ms = 250;

      json_transport_ = std::make_unique<audio_studio::rpc::SocketJsonRpcTransport>(drivers.socket(), endpoint);
      stream_transport_ = std::make_unique<audio_studio::rpc::SocketRpcStreamTransport>(drivers.socket(), endpoint);
      json_client_ = std::make_unique<audio_studio::rpc::JsonRpcClient>(*json_transport_);
      audio_client_ = std::make_unique<audio_studio::rpc::AudioRpcClient>(*json_client_, stream_transport_.get());

      audio_studio::rpc::AudioSessionConfig config;
      config.session_id = simpleJsonStringField(request_json, "session_id", "gui_playback");
      config.sample_rate = static_cast<uint32_t>(simpleJsonIntField(request_json, "sample_rate",
                            simpleJsonIntField(request_json, "sampleRate", 48000)));
      config.channels = static_cast<uint16_t>(simpleJsonIntField(request_json, "channels", 2));
      const int bits = simpleJsonIntField(request_json, "bits_per_sample",
                       simpleJsonIntField(request_json, "bits", 16));
      config.bytes_per_sample = static_cast<uint16_t>(std::max(1, bits / 8));
      config.device_name = simpleJsonStringField(request_json, "device", "as_config_playback");
      config.driver_factory = runtimeAudioDriverFactory(request_json, config_);
      config.blocking_write = true;
      playback_ = std::make_unique<audio_studio::rpc::AudioPlayback>(audio_client_->createPlaybackSession(config));
      playback_->start();
      std::lock_guard<std::mutex> lock(mutex_);
      rpc_ready_ = true;
    } catch (const std::exception& error) {
      const std::string message = error.what();
      cleanupRpc();
      std::lock_guard<std::mutex> lock(mutex_);
      rpc_ready_ = false;
      error_ = message;
    }
#else
    (void)request_json;
    std::lock_guard<std::mutex> lock(mutex_);
    rpc_ready_ = false;
    error_ = "GUI backend was built without RPC playback client support";
#endif
  }

  void cleanupRpc() {
#if defined(AUDIO_STUDIO_GUI_PLAYBACK_RPC)
    try {
      if (playback_) {
        playback_->stop();
        playback_->close();
      }
    } catch (...) {
    }
    playback_.reset();
    audio_client_.reset();
    json_client_.reset();
    stream_transport_.reset();
    json_transport_.reset();
#endif
  }

  void finishRpc() {
#if defined(AUDIO_STUDIO_GUI_PLAYBACK_RPC)
    try {
      if (playback_) {
        playback_->drain();
        playback_->stop();
        playback_->close();
      }
    } catch (...) {
    }
    playback_.reset();
    audio_client_.reset();
    json_client_.reset();
    stream_transport_.reset();
    json_transport_.reset();
#endif
  }

  void run() {
    while (true) {
      Frame frame;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&] { return stopping_ || (!queue_.empty() && rpc_ready_); });
        if (stopping_) break;
        frame = std::move(queue_.front());
        queue_.pop_front();
        queued_bytes_ -= frame.bytes.size();
        in_flight_ = true;
        last_dequeue_ms_ = nowMs();
      }

#if defined(AUDIO_STUDIO_GUI_PLAYBACK_RPC)
      try {
        if (playback_) {
          auto written = playback_->writeFrames(frame.bytes);
          std::lock_guard<std::mutex> lock(mutex_);
          played_bytes_ += written.bytes;
        }
      } catch (const std::exception& error) {
        std::lock_guard<std::mutex> lock(mutex_);
        rpc_ready_ = false;
        error_ = error.what();
      }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        in_flight_ = false;
      }
      cv_.notify_all();
#else
      (void)frame;
#endif
    }
  }

  std::string statusJsonLocked(bool ok) const {
    std::ostringstream os;
    os << "{\"ok\":" << (ok ? "true" : "false")
       << ",\"running\":" << (ok && active_ ? "true" : "false")
       << ",\"runtime_state\":\"" << (ok && active_ ? "PIPE_RUNNING" : "PIPE_LOADED") << "\""
       << ",\"playback\":{\"active\":" << (active_ ? "true" : "false")
       << ",\"rpc_ready\":" << (rpc_ready_ ? "true" : "false")
       << ",\"queued_bytes\":" << queued_bytes_;
    if (!error_.empty()) os << ",\"error\":\"" << jsonEscape(error_) << "\"";
    os << "}}";
    return os.str();
  }

  std::string statusJson(bool ok) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return statusJsonLocked(ok);
  }

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Frame> queue_;
  std::thread worker_;
  bool active_ = false;
  bool stopping_ = true;
  bool eof_requested_ = false;
  bool in_flight_ = false;
  bool rpc_ready_ = false;
  size_t queued_bytes_ = 0;
  uint64_t played_bytes_ = 0;
  int64_t last_dequeue_ms_ = 0;
  int next_push_ms_ = 20;
  int sample_frame_bytes_ = 4;
  int sample_rate_ = 48000;
  BackendRuntimeConfig config_;
  std::string session_id_;
  std::string blocked_edge_key_;
  std::string error_;
  std::mutex system_info_mutex_;
  std::map<std::string, BufferProgress> system_buffer_progress_;

#if defined(AUDIO_STUDIO_GUI_PLAYBACK_RPC)
  audio_studio::rpc::SocketRpcEndpoint system_info_endpoint_;
  std::unique_ptr<audio_studio::rpc::SocketJsonRpcTransport> json_transport_;
  std::unique_ptr<audio_studio::rpc::SocketRpcStreamTransport> stream_transport_;
  std::unique_ptr<audio_studio::rpc::JsonRpcClient> json_client_;
  std::unique_ptr<audio_studio::rpc::AudioRpcClient> audio_client_;
  std::unique_ptr<audio_studio::rpc::AudioPlayback> playback_;
#endif
};

struct GuiRuntimeEngine::CaptureWorker {
  explicit CaptureWorker(BackendRuntimeConfig config)
    : config_(std::move(config)) {}

  ~CaptureWorker() {
    stop();
  }

  std::string start(const std::string& request_json) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (active_ && rpc_ready_) return statusJsonLocked();
    }
    stop();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      active_ = true;
      error_.clear();
      captured_bytes_ = 0;
      session_id_ = simpleJsonStringField(request_json, "session_id", "gui_capture");
      node_id_ = simpleJsonStringField(request_json, "node_id");
      file_name_ = simpleJsonStringField(request_json, "file_name", "capture.wav");
      frame_bytes_ = static_cast<size_t>(std::max(256, simpleJsonIntField(request_json, "frame_bytes", 3840)));
      sample_frame_bytes_ = inferSampleFrameBytes(request_json);
      sample_rate_ = inferSampleRate(request_json);
    }

    setupRpc(request_json);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!rpc_ready_) {
        active_ = false;
      }
    }
    return statusJson();
  }

  void stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      active_ = false;
    }
    cleanupRpc();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      rpc_ready_ = false;
    }
  }

  std::string pull(const std::string& query) {
    const auto q = parseQuery(query);
    const size_t max_bytes = static_cast<size_t>(std::max(1, simpleJsonIntField(
        "{\"max_bytes\":" + (q.count("max_bytes") ? q.at("max_bytes") : std::string("0")) + "}", "max_bytes",
        static_cast<int>(frame_bytes_))));

    std::vector<uint8_t> frame;
    bool active = false;
    bool rpc_ready = false;
    std::string error;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      active = active_;
      rpc_ready = rpc_ready_;
      error = error_;
    }

    if (active && rpc_ready && error.empty()) {
#if defined(AUDIO_STUDIO_GUI_PLAYBACK_RPC)
      try {
        auto result = capture_ ? capture_->readFrames(max_bytes, {1000}) : audio_studio::rpc::AudioReadResult{};
        if (result.ok && !result.data.empty()) {
          frame = std::move(result.data);
          std::lock_guard<std::mutex> lock(mutex_);
          captured_bytes_ += frame.size();
        } else if (!result.ok) {
          std::lock_guard<std::mutex> lock(mutex_);
          error_ = "capture stream read failed";
          rpc_ready_ = false;
          error = error_;
          rpc_ready = false;
        }
      } catch (const std::exception& read_error) {
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = read_error.what();
        rpc_ready_ = false;
        error = error_;
        rpc_ready = false;
      }
#endif
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      active = active_;
      rpc_ready = rpc_ready_;
      error = error_;
    }
    const bool ok = error.empty() || !frame.empty();
    const int next_poll_ms = frame.empty() ? 20 : frameDurationMs(frame.size(), sample_frame_bytes_, sample_rate_);
    std::ostringstream os;
    os << "{\"ok\":" << (ok ? "true" : "false")
       << ",\"capture\":{\"active\":" << (active ? "true" : "false")
       << ",\"rpc_ready\":" << (rpc_ready ? "true" : "false")
       << ",\"bytes\":" << frame.size()
       << ",\"queued_bytes\":0"
       << ",\"next_poll_ms\":" << next_poll_ms
       << ",\"data_base64\":\"" << base64Encode(frame) << "\"";
    if (!error.empty()) os << ",\"error\":\"" << jsonEscape(error) << "\"";
    os << "}}";
    return os.str();
  }

private:
  static int inferSampleFrameBytes(const std::string& request_json) {
    const int channels = std::max(1, simpleJsonIntField(request_json, "channels", 2));
    const int bits = simpleJsonIntField(request_json, "bits_per_sample",
                     simpleJsonIntField(request_json, "bits", 16));
    return std::max(1, channels * std::max(1, bits / 8));
  }

  static int inferSampleRate(const std::string& request_json) {
    return std::max(1, simpleJsonIntField(request_json, "sample_rate",
                       simpleJsonIntField(request_json, "sampleRate", 48000)));
  }

  static int frameDurationMs(size_t bytes, int sample_frame_bytes, int sample_rate) {
    const double bytes_per_ms =
        std::max(1, sample_frame_bytes) * static_cast<double>(std::max(1, sample_rate)) / 1000.0;
    return std::max(1, std::min(250,
        static_cast<int>(std::lround(static_cast<double>(bytes) / bytes_per_ms))));
  }

  void setupRpc(const std::string& request_json) {
#if defined(AUDIO_STUDIO_GUI_PLAYBACK_RPC)
    try {
      auto& drivers = audio_studio::drivers::DriverManager::instance();
      audio_studio::drivers::DriverManagerConfig driver_config;
      driver_config.enable_os = false;
      driver_config.enable_socket = true;
      driver_config.enable_filesystem = false;
      driver_config.enable_pipe = false;
      driver_config.enable_dynlib = false;
      driver_config.enable_datalink = false;
      driver_config.enable_audio = false;
      driver_config.enable_control = false;
      driver_config.enable_log = false;
      driver_config.enable_dump = false;
      auto status = drivers.initialize(driver_config);
      if (!status.ok()) throw std::runtime_error(status.message());

      audio_studio::rpc::SocketRpcEndpoint endpoint;
      endpoint.host = simpleJsonStringField(request_json, "as_server_host", config_.as_server_host);
      endpoint.port = static_cast<uint16_t>(simpleJsonIntField(
          request_json, "as_server_port", config_.as_server_port));
      endpoint.timeout_ms = 1000;

      json_transport_ = std::make_unique<audio_studio::rpc::SocketJsonRpcTransport>(drivers.socket(), endpoint);
      stream_transport_ = std::make_unique<audio_studio::rpc::SocketRpcStreamTransport>(drivers.socket(), endpoint);
      json_client_ = std::make_unique<audio_studio::rpc::JsonRpcClient>(*json_transport_);
      audio_client_ = std::make_unique<audio_studio::rpc::AudioRpcClient>(*json_client_, stream_transport_.get());

      audio_studio::rpc::AudioSessionConfig config;
      config.session_id = simpleJsonStringField(request_json, "session_id", "gui_capture");
      config.sample_rate = static_cast<uint32_t>(simpleJsonIntField(request_json, "sample_rate",
                            simpleJsonIntField(request_json, "sampleRate", 48000)));
      config.channels = static_cast<uint16_t>(simpleJsonIntField(request_json, "channels", 2));
      const int bits = simpleJsonIntField(request_json, "bits_per_sample",
                       simpleJsonIntField(request_json, "bits", 16));
      config.bytes_per_sample = static_cast<uint16_t>(std::max(1, bits / 8));
      config.device_name = simpleJsonStringField(request_json, "device", "as_config_capture");
      config.driver_factory = runtimeAudioDriverFactory(request_json, config_);
      capture_ = std::make_unique<audio_studio::rpc::AudioCapture>(audio_client_->createCaptureSession(config));
      capture_->start();
      std::lock_guard<std::mutex> lock(mutex_);
      rpc_ready_ = true;
    } catch (const std::exception& error) {
      const std::string message = error.what();
      cleanupRpc();
      std::lock_guard<std::mutex> lock(mutex_);
      rpc_ready_ = false;
      error_ = message;
    }
#else
    (void)request_json;
    std::lock_guard<std::mutex> lock(mutex_);
    rpc_ready_ = false;
    error_ = "GUI backend was built without RPC capture client support";
#endif
  }

  void cleanupRpc() {
#if defined(AUDIO_STUDIO_GUI_PLAYBACK_RPC)
    try {
      if (capture_) {
        capture_->stop();
        capture_->close();
      }
    } catch (...) {
    }
    capture_.reset();
    audio_client_.reset();
    json_client_.reset();
    stream_transport_.reset();
    json_transport_.reset();
#endif
  }

  std::string statusJsonLocked() const {
    const bool ok = error_.empty();
    std::ostringstream os;
    os << "{\"ok\":" << (ok ? "true" : "false")
       << ",\"running\":" << (ok ? "true" : "false")
       << ",\"runtime_state\":\"" << (ok ? "PIPE_RUNNING" : "PIPE_LOADED") << "\""
       << ",\"capture\":{\"active\":" << (active_ ? "true" : "false")
       << ",\"rpc_ready\":" << (rpc_ready_ ? "true" : "false")
       << ",\"queued_bytes\":0"
       << ",\"captured_bytes\":" << captured_bytes_
       << ",\"node_id\":\"" << jsonEscape(node_id_) << "\""
       << ",\"file_name\":\"" << jsonEscape(file_name_) << "\"";
    if (!error_.empty()) os << ",\"error\":\"" << jsonEscape(error_) << "\"";
    os << "}}";
    return os.str();
  }

  std::string statusJson() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return statusJsonLocked();
  }

  mutable std::mutex mutex_;
  bool active_ = false;
  bool rpc_ready_ = false;
  size_t frame_bytes_ = 3840;
  int sample_frame_bytes_ = 4;
  int sample_rate_ = 48000;
  uint64_t captured_bytes_ = 0;
  BackendRuntimeConfig config_;
  std::string session_id_;
  std::string node_id_;
  std::string file_name_;
  std::string error_;

#if defined(AUDIO_STUDIO_GUI_PLAYBACK_RPC)
  std::unique_ptr<audio_studio::rpc::SocketJsonRpcTransport> json_transport_;
  std::unique_ptr<audio_studio::rpc::SocketRpcStreamTransport> stream_transport_;
  std::unique_ptr<audio_studio::rpc::JsonRpcClient> json_client_;
  std::unique_ptr<audio_studio::rpc::AudioRpcClient> audio_client_;
  std::unique_ptr<audio_studio::rpc::AudioCapture> capture_;
#endif
};

GuiRuntimeEngine::GuiRuntimeEngine(std::shared_ptr<BuildOrchestrator> build_orchestrator,
                                   BackendRuntimeConfig config)
  : build_orchestrator_(std::move(build_orchestrator)),
    config_(std::move(config)) {
  if (!build_orchestrator_) build_orchestrator_ = std::make_shared<BuildOrchestrator>(".", nullptr, nullptr, config_);
  playback_ = std::make_unique<PlaybackWorker>(config_);
  capture_ = std::make_unique<CaptureWorker>(config_);
  rng_.seed(static_cast<unsigned>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
}

GuiRuntimeEngine::~GuiRuntimeEngine() = default;

double GuiRuntimeEngine::rnd(double min, double max) {
  std::lock_guard<std::mutex> lk(rng_mutex_);
  std::uniform_real_distribution<double> dist(min, max);
  return dist(rng_);
}

int GuiRuntimeEngine::rndi(int min, int max) {
  std::lock_guard<std::mutex> lk(rng_mutex_);
  std::uniform_int_distribution<int> dist(min, max);
  return dist(rng_);
}

std::string GuiRuntimeEngine::validatePipeline(const std::string& pipeline_json) {
  const bool has_nodes = pipeline_json.find("nodes") != std::string::npos;
  std::ostringstream os;
  os << "{\"ok\":" << (has_nodes ? "true" : "false")
     << ",\"warnings\":[],\"errors\":";
  if (has_nodes) os << "[]";
  else os << "[\"No node section found\"]";
  os << "}";
  return os.str();
}

std::string GuiRuntimeEngine::buildPipeline(const std::string& pipeline_json) {
  return build_orchestrator_->buildPipeline(pipeline_json).body;
}

std::string GuiRuntimeEngine::unloadPipeline(const std::string& pipeline_json) {
  return build_orchestrator_->unloadPipeline(pipeline_json).body;
}

std::string GuiRuntimeEngine::run(const std::string& session_id) {
  running_.store(true);
  const bool wants_capture = !jsonMemberValue(session_id, "capture").empty();
  const bool wants_playback = !jsonMemberValue(session_id, "playback").empty() || !wants_capture;
  std::string playback_status;
  std::string capture_status;
  if (wants_playback) playback_status = playback_->start(directionRuntimeRequest(session_id, "playback"));
  if (wants_capture) capture_status = capture_->start(directionRuntimeRequest(session_id, "capture"));
  if (wants_playback && !wants_capture) {
    if (playback_status.find("\"ok\":false") != std::string::npos) running_.store(false);
    return playback_status;
  }
  if (wants_capture && !wants_playback) {
    if (capture_status.find("\"ok\":false") != std::string::npos) running_.store(false);
    return capture_status;
  }
  const bool ok = playback_status.find("\"ok\":false") == std::string::npos &&
                  capture_status.find("\"ok\":false") == std::string::npos;
  if (!ok) running_.store(false);
  std::ostringstream os;
  os << "{\"ok\":" << (ok ? "true" : "false")
     << ",\"running\":" << (ok ? "true" : "false")
     << ",\"runtime_state\":\"" << (ok ? "PIPE_RUNNING" : "PIPE_LOADED") << "\"";
  const std::string playback_json = jsonMemberValue(playback_status, "playback");
  const std::string capture_json = jsonMemberValue(capture_status, "capture");
  if (!playback_json.empty()) os << ",\"playback\":" << playback_json;
  if (!capture_json.empty()) os << ",\"capture\":" << capture_json;
  os << "}";
  return os.str();
}

std::string GuiRuntimeEngine::stop(const std::string& session_id) {
  const bool was_running = running_.exchange(false);
  playback_->stop();
  capture_->stop();
  const std::string requested = simpleJsonStringField(session_id, "runtime_state", "PIPE_UNLOADED");
  std::string next_state = "PIPE_UNLOADED";
  if (was_running || requested == "PIPE_RUNNING") {
    next_state = "PIPE_LOADED";
  } else if (requested == "PIPE_LOADED" || requested == "PIPE_UNLOADED" ||
             requested == "NOT_READY" || requested == "ERROR") {
    next_state = requested;
  }
  return "{\"ok\":true,\"running\":false,\"runtime_state\":\"" + next_state + "\"}";
}

std::string GuiRuntimeEngine::pushAudioStream(const std::string& query, const std::string& frame) {
  return playback_->enqueueStream(query, frame);
}

std::string GuiRuntimeEngine::pushAudioFrame(const std::string& query, const std::string& frame) {
  return playback_->frameStatus(query, frame);
}

std::string GuiRuntimeEngine::finishAudioInput(const std::string& request_json) {
  running_.store(false);
  return playback_->finish(request_json);
}

std::string GuiRuntimeEngine::captureAudioFrame(const std::string& query) {
  return capture_->pull(query);
}

std::string GuiRuntimeEngine::pipelineEditEvent(const std::string& request_json) {
  (void)request_json;
  return "{\"ok\":true,\"callback\":\"IRuntimeEngine::pipelineEditEvent\",\"message\":\"edit event accepted\"}";
}

std::string GuiRuntimeEngine::pipelineToolAction(const std::string& request_json) {
  (void)request_json;
  return "{\"ok\":true,\"callback\":\"IRuntimeEngine::pipelineToolAction\",\"message\":\"tool action accepted\"}";
}

std::string GuiRuntimeEngine::telemetry(const std::vector<std::string>& node_ids) {
  const bool run = running_.load();
  std::ostringstream os;
  os << std::fixed << std::setprecision(2);
  os << "{\"running\":" << (run ? "true" : "false")
     << ",\"timestamp\":" << static_cast<long long>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch()).count());
  os << ",\"nodeCost\":{";
  for (size_t i = 0; i < node_ids.size(); ++i) {
    if (i) os << ",";
    os << "\"" << jsonEscape(node_ids[i]) << "\":{"
       << "\"cpu\":" << (run ? rnd(0.2, 8.8) : rnd(0.0, 0.18))
       << ",\"memKb\":" << rndi(64, 720)
       << ",\"latencyMs\":" << (run ? rnd(0.04, 2.8) : 0.0)
       << ",\"core\":" << rndi(0, 3)
       << ",\"rms\":" << rnd(-35, -8)
       << ",\"peak\":" << rnd(-12, -0.4) << "}";
  }
  os << "},\"cores\":[";
  for (int i = 0; i < 4; ++i) {
    if (i) os << ",";
    os << "{\"id\":" << i
       << ",\"load\":" << (run ? rnd(12, 86) : rnd(0, 5))
       << ",\"temperature\":" << (run ? rnd(42, 66) : rnd(35, 40))
       << ",\"powerMw\":" << (run ? rndi(420, 1700) : rndi(60, 180)) << "}";
  }
  os << "],\"health\":{"
     << "\"latencyMs\":" << (run ? rnd(14, 23) : 0)
     << ",\"bufferOccupancy\":" << (run ? rnd(30, 58) : 0)
     << ",\"throughput\":" << (run ? rnd(88, 108) : 0)
     << ",\"xruns\":0"
     << ",\"memoryMb\":" << rnd(260, 365)
     << ",\"powerW\":" << (run ? rnd(3.1, 4.8) : rnd(0.25, 0.55)) << "}";
  os << ",\"meters\":{"
     << "\"inL\":" << (run ? -rnd(8, 28) : -60)
     << ",\"inR\":" << (run ? -rnd(8, 28) : -60)
     << ",\"outL\":" << (run ? -rnd(4, 18) : -60)
     << ",\"outR\":" << (run ? -rnd(4, 18) : -60) << "}}";
  return os.str();
}

} // namespace audiostudio
