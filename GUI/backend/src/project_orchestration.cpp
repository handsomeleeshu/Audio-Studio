#include "audio_studio.hpp"

#include <cerrno>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
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
std::string mergeSnapshot(const std::string& workspace_json, const std::string& snapshot_json);

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
  std::string inst_ref;
  std::string kind;
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

bool isDebugFileModule(const std::string& module_type) {
  std::string id;
  id.reserve(module_type.size());
  for (char c : module_type) id.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return id == "builtin.file_input" || id == "builtin.file_output" ||
         id == "virtual.file_input" || id == "virtual.audio_output";
}

bool isHostOrDaiModule(const std::string& module_type) {
  std::string id;
  id.reserve(module_type.size());
  for (char c : module_type) id.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return id == "builtin.host" || id == "host" || id == "builtin.dai" || id == "dai";
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
    node.inst_ref = jsonStringFieldAny(object, {"inst_ref", "instId", "inst_id"});
    node.kind = simpleJsonStringField(object, "kind", node.inst_ref.empty() ? "module_inline" : "module");
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

bool snapshotGroupMatchesTarget(const SnapshotGroup& group, const std::string& target) {
  if (target.empty() || target == "ALL") return true;
  if (group.id == target || group.pipeline_id == target) return true;
  return std::find(group.origin_pipeline_ids.begin(), group.origin_pipeline_ids.end(), target) !=
         group.origin_pipeline_ids.end();
}

bool regeneratePipelinesFromSnapshot(const std::string& workspace_json,
                                      const std::string& snapshot_json,
                                      std::string& pipelines_json,
                                      std::set<std::string>& generated_node_keys,
                                      std::vector<std::string>& diagnostics) {
  const auto originals = originalPipelineObjects(workspace_json);
  const auto nodes = snapshotNodes(snapshot_json);
  const auto connections = snapshotConnections(snapshot_json);
  auto groups = snapshotGroups(snapshot_json);
  if (groups.empty()) {
    diagnostics.push_back("snapshot.working_groups is required to regenerate platform pipelines");
    return false;
  }
  const std::string build_target = jsonStringFieldAny(snapshot_json, {"group_id", "groupId"},
      jsonStringFieldAny(snapshot_json, {"active_pipeline", "activePipeline"}, "ALL"));
  if (!build_target.empty() && build_target != "ALL") {
    std::vector<SnapshotGroup> selected_groups;
    for (const auto& group : groups) {
      if (snapshotGroupMatchesTarget(group, build_target)) selected_groups.push_back(group);
    }
    if (selected_groups.empty()) {
      diagnostics.push_back("snapshot working group not found: " + build_target);
      return false;
    }
    groups = std::move(selected_groups);
  }
  std::map<std::string, SnapshotNode> node_by_graph_id;
  for (const auto& node : nodes) node_by_graph_id[node.graph_id] = node;

  const std::string runtime_fallback = firstPipelineFieldOrDefault(originals, "runtime",
      "{\"core_ref\":\"audio_core0\",\"priority\":0,\"clock\":\"timer\"}");
  std::ostringstream out;
  out << "[";
  for (size_t gi = 0; gi < groups.size(); ++gi) {
    auto& group = groups[gi];
    if (gi) out << ",";

    std::string origin_pipe_id;
    if (group.origin_pipeline_ids.size() == 1 && originals.count(group.origin_pipeline_ids.front())) {
      origin_pipe_id = group.origin_pipeline_ids.front();
    } else if (originals.count(group.pipeline_id)) {
      origin_pipe_id = group.pipeline_id;
    }
    const std::string pipe_id = origin_pipe_id.empty() ? ("GUI_PIPE_" + std::to_string(gi + 1)) : origin_pipe_id;
    const std::string pipe_name = !group.name.empty()
      ? group.name
      : (origin_pipe_id.empty() ? ("GUI Pipeline " + std::to_string(gi + 1))
                                : simpleJsonStringField(originals.at(origin_pipe_id), "name", origin_pipe_id));

    if (group.node_ids.empty()) {
      for (const auto& node : nodes) {
        if (node.pipeline_id == group.pipeline_id || (!origin_pipe_id.empty() && node.pipeline_id == origin_pipe_id)) {
          group.node_ids.push_back(node.graph_id);
        }
      }
    }

    std::map<std::string, std::string> graph_to_local;
    std::set<std::string> included_graph_ids;
    std::vector<std::string> node_jsons;
    for (const auto& graph_id : group.node_ids) {
      const auto it = node_by_graph_id.find(graph_id);
      if (it == node_by_graph_id.end()) continue;
      const SnapshotNode& node = it->second;
      if (node.debug_file_io) continue;
      if (isHostOrDaiModule(node.module_type) && node.inst_ref.empty()) {
        diagnostics.push_back("HOST/DAI node requires inst_ref: " + node.graph_id);
        return false;
      }
      const std::string local_id = graphLocalNodeId(node, pipe_id);
      graph_to_local[node.graph_id] = local_id;
      included_graph_ids.insert(node.graph_id);
      generated_node_keys.insert(pipe_id + "." + local_id);
      std::ostringstream node_os;
      node_os << "{\"node_id\":\"" << jsonEscape(local_id) << "\"";
      if (!node.inst_ref.empty()) {
        node_os << ",\"kind\":\"module\",\"inst_ref\":\"" << jsonEscape(node.inst_ref) << "\"";
      } else {
        node_os << ",\"kind\":\"module_inline\",\"inline\":{\"module_type\":\""
                << jsonEscape(node.module_type.empty() ? "unknown" : node.module_type)
                << "\",\"name\":\"" << jsonEscape(node.name.empty() ? local_id : node.name) << "\"}";
      }
      node_os << "}";
      node_jsons.push_back(node_os.str());
    }

    std::vector<std::string> edge_jsons;
    for (const auto& conn : connections) {
      if (!included_graph_ids.count(conn.from_node) || !included_graph_ids.count(conn.to_node)) continue;
      const auto from_node_it = node_by_graph_id.find(conn.from_node);
      const auto to_node_it = node_by_graph_id.find(conn.to_node);
      if (from_node_it == node_by_graph_id.end() || to_node_it == node_by_graph_id.end()) continue;
      const std::string from_domain = canonicalPortDomain(conn.from_domain.empty()
        ? nodePortDomain(from_node_it->second, conn.from_port)
        : conn.from_domain);
      const std::string to_domain = canonicalPortDomain(conn.to_domain.empty()
        ? nodePortDomain(to_node_it->second, conn.to_port)
        : conn.to_domain);
      if (from_domain != "sof" || to_domain != "sof") continue;
      edge_jsons.push_back("{\"from\":\"" + jsonEscape(graph_to_local[conn.from_node] + ":" + conn.from_port) +
                           "\",\"to\":\"" + jsonEscape(graph_to_local[conn.to_node] + ":" + conn.to_port) + "\"}");
    }

    out << "{\"pipe_id\":\"" << jsonEscape(pipe_id) << "\","
        << "\"name\":\"" << jsonEscape(pipe_name) << "\","
        << "\"domain\":" << pipelineFieldOrDefault(originals, origin_pipe_id, "domain", "\"playback\"") << ","
        << "\"frame\":" << pipelineFieldOrDefault(originals, origin_pipe_id, "frame", "{\"rate\":48000,\"block_ms\":4}") << ","
        << "\"runtime\":" << pipelineFieldOrDefault(originals, origin_pipe_id, "runtime", runtime_fallback) << ","
        << "\"nodes\":[";
    for (size_t i = 0; i < node_jsons.size(); ++i) {
      if (i) out << ",";
      out << node_jsons[i];
    }
    out << "],\"edges\":[";
    for (size_t i = 0; i < edge_jsons.size(); ++i) {
      if (i) out << ",";
      out << edge_jsons[i];
    }
    out << "]}";
  }
  out << "]";
  pipelines_json = out.str();
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

std::string mergeSnapshotWithRegeneratedPipelines(const std::string& workspace_json,
                                                  const std::string& snapshot_json,
                                                  const std::string& pipelines_json) {
  return mergeSnapshot(upsertObjectMember(workspace_json, "pipelines", pipelines_json), snapshot_json);
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

std::string buildExtraFields(const std::string& pipeline_object, uint64_t workspace_revision) {
  std::ostringstream os;
  os << "\"updated_pipeline\":" << (pipeline_object.empty() ? "{}" : pipeline_object)
     << ",\"workspace_revision\":" << workspace_revision;
  return os.str();
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

std::string firstExistingPath(const std::vector<fs::path>& candidates) {
  for (const auto& candidate : candidates) {
    if (candidate.empty()) continue;
    if (fs::exists(candidate)) return candidate.string();
  }
  return {};
}

std::vector<fs::path> pathAndParents(fs::path path, size_t max_depth = 8) {
  std::vector<fs::path> out;
  if (path.empty()) return out;
  std::error_code ec;
  path = fs::absolute(path, ec);
  if (ec) return out;
  for (size_t i = 0; i < max_depth && !path.empty(); ++i) {
    out.push_back(path);
    const fs::path parent = path.parent_path();
    if (parent == path) break;
    path = parent;
  }
  return out;
}

std::string firstExistingRelativeToRoots(const std::vector<fs::path>& roots,
                                         const std::vector<fs::path>& relatives) {
  std::vector<fs::path> candidates;
  for (const auto& root : roots) {
    for (const auto& rel : relatives) {
      candidates.push_back(root / rel);
    }
  }
  return firstExistingPath(candidates);
}

std::string resolveCompileServerPath(const std::string& root_dir) {
  const std::string env_path = envString("AUDIO_STUDIO_AS_SERVER_PATH", "");
  if (!env_path.empty() && fs::exists(env_path)) return env_path;
  auto roots = pathAndParents(root_dir);
  const auto cwd_roots = pathAndParents(fs::current_path());
  roots.insert(roots.end(), cwd_roots.begin(), cwd_roots.end());
  const std::string resolved = firstExistingRelativeToRoots(roots, {
      "out/linux/a2/as_config/Debug/as_server",
      "out/linux/a2/rpc_socket/Debug/as_server",
      "out/linux/simulator/rpc_socket/Debug/as_server",
      "as_config/Debug/as_server",
      "rpc_socket/Debug/as_server"});
  if (!resolved.empty()) return resolved;
  return (fs::path(root_dir) / "out/linux/simulator/rpc_socket/Debug/as_server").string();
}

std::string resolveSimulatorRpcToolPath(const std::string& root_dir,
                                        const std::string& tool,
                                        const char* env_name) {
  const std::string env_path = envString(env_name, "");
  if (!env_path.empty() && fs::exists(env_path)) return env_path;
  auto roots = pathAndParents(root_dir);
  const auto cwd_roots = pathAndParents(fs::current_path());
  roots.insert(roots.end(), cwd_roots.begin(), cwd_roots.end());
  const std::string resolved = firstExistingRelativeToRoots(roots, {
      fs::path("out/linux/simulator/rpc_socket/Debug") / tool,
      fs::path("out/linux/a2/rpc_socket/Debug") / tool,
      fs::path("rpc_socket/Debug") / tool});
  if (!resolved.empty()) return resolved;
  return (fs::path(root_dir) / "out/linux/simulator/rpc_socket/Debug" / tool).string();
}

std::string resolveTraceLdcPath(const std::string& root_dir) {
  const std::string env_path = envString("AUDIO_STUDIO_VALIDATION_TRACE_LDC", "");
  if (!env_path.empty() && fs::exists(env_path)) return env_path;
  auto roots = pathAndParents(root_dir);
  const auto cwd_roots = pathAndParents(fs::current_path());
  roots.insert(roots.end(), cwd_roots.begin(), cwd_roots.end());
  const std::string resolved = firstExistingRelativeToRoots(roots, {
      "application/rv32qemu/build/sof.ldc",
      "../application/rv32qemu/build/sof.ldc"});
  if (!resolved.empty()) return resolved;
  return (fs::path(root_dir) / "../application/rv32qemu/build/sof.ldc").string();
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
  auto roots = pathAndParents(root_dir);
  const auto cwd_roots = pathAndParents(fs::current_path());
  roots.insert(roots.end(), cwd_roots.begin(), cwd_roots.end());
  const std::string resolved = firstExistingRelativeToRoots(roots, {
      "application/rv32qemu/sof-build-test.py",
      "../application/rv32qemu/sof-build-test.py"});
  if (!resolved.empty()) return resolved;
  return (fs::path(root_dir) / "../application/rv32qemu/sof-build-test.py").string();
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
                             const std::string& rpc_request,
                             std::string& response,
                             std::string& error) {
  if (as_server.empty() || !fs::exists(as_server)) {
    error = as_server.empty() ? "as_server --rpc-once executable not found"
                              : "as_server --rpc-once executable not found: " + as_server;
    return false;
  }
  const std::string cmd = shellQuote(as_server) + " --rpc-once " + shellQuote(rpc_request) + " 2>&1";
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

  std::string error;
  const int fd = connectTcp(host_, port_, timeout_ms_, error);
  if (fd < 0) {
    const std::string socket_error = error.empty() ? "failed to connect as_server config.compile RPC" : error;
    std::string rpc_once_response;
    std::string rpc_once_error;
    if (runConfigCompileRpcOnce(request.as_server, rpc_request, rpc_once_response, rpc_once_error)) {
      result = parseConfigCompileResponse(rpc_once_response);
      if (!result.ok) result.diagnostics.push_back("socket_rpc_error=" + socket_error);
      return result;
    }
    result.ok = false;
    result.message = socket_error + "; " + rpc_once_error;
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
      (request.as_server_path.empty() ? "" : " --as-server " + shellQuote(request.as_server_path)) +
      (request.as_log_path.empty() ? "" : " --as-log " + shellQuote(request.as_log_path)) +
      (request.trace_ldc_path.empty() ? "" : " --trace-ldc " + shellQuote(request.trace_ldc_path)) +
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
  const std::string snapshot_json = snapshot.empty() ? request_json : snapshot;
  std::string regenerated_pipelines;
  std::set<std::string> generated_node_keys;
  std::vector<std::string> workspace_diagnostics;
  if (!regeneratePipelinesFromSnapshot(all_json, snapshot_json, regenerated_pipelines, generated_node_keys, workspace_diagnostics)) {
    return {200, "application/json; charset=utf-8",
            failureResponse("workspace",
                            workspace_diagnostics.empty() ? "failed to regenerate workspace pipelines" : workspace_diagnostics.front(),
                            request_json,
                            workspace_diagnostics)};
  }
  const std::string pipeline_object = singlePipelineObjectFromArray(regenerated_pipelines);
  if (pipeline_object.empty()) {
    return {200, "application/json; charset=utf-8",
            failureResponse("workspace", "build requires exactly one selected pipeline", request_json)};
  }
  const std::string pipeline_id = pipelineIdentity(pipeline_object);
  uint64_t workspace_revision = record.workspace_revision;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    auto* found = findWorkspaceLocked(record.workspace_id);
    if (!found) {
      return {404, "application/json; charset=utf-8",
              failureResponse("open", "workspace_id not found", request_json)};
    }
    if (found->loaded_pipeline_ids.count(pipeline_id)) {
      return {200, "application/json; charset=utf-8",
              failureResponse("runtime",
                              "pipeline already_loaded; unload before building again",
                              request_json,
                              {},
                              buildExtraFields(pipeline_object, found->workspace_revision))};
    }
    record = *found;
    workspace_revision = found->workspace_revision;
  }

  const std::string single_file_name = record.project_name + "_pipeline_" + safeId(pipeline_id) + ".json";
  const std::string single_input_path = (fs::path(record.workspace_dir) / single_file_name).string();
  const std::string single_output_dir = (fs::path(record.output_dir) / safeId(pipeline_id)).string();
  const std::string single_json = mergeSnapshot(
      upsertObjectMember(filterPresetNodeValues(all_json, generated_node_keys),
                         "pipelines", "[" + pipeline_object + "]"),
      snapshot_json);
  const std::string all_json_next = mergeSnapshot(upsertPipelineObject(all_json, pipeline_object), snapshot_json);
  if (!writeTextFile(single_input_path, single_json) ||
      !writeTextFile(record.input_path, all_json_next)) {
    return {500, "application/json; charset=utf-8",
            failureResponse("workspace", "failed to update workspace JSON", request_json)};
  }

  {
    std::lock_guard<std::mutex> lk(mutex_);
    auto* found = findWorkspaceLocked(record.workspace_id);
    if (found) {
      found->dirty_pipeline_ids.insert(pipeline_id);
      found->loaded_pipeline_ids.erase(pipeline_id);
      found->validated_pipeline_ids.erase(pipeline_id);
      found->build_ok = false;
      found->workspace_revision += 1;
      workspace_revision = found->workspace_revision;
      record = *found;
    }
  }

  GuiConfigCompileRequest compile_request;
  compile_request.input_path = single_input_path;
  compile_request.output_dir = single_output_dir;
  compile_request.project_name = record.project_name;
  compile_request.alsatplg = (fs::path(root_dir_) / "third_party/alsatplg/bin/alsatplg").string();
  compile_request.as_server = resolveCompileServerPath(root_dir_);
  compile_request.build_tplg = true;
  compile_request.strict = true;
  compile_request.plugin_paths = {};

  auto compile_result = compile_client_->compile(compile_request);
  if (!compile_result.ok) {
    return {200, "application/json; charset=utf-8",
            failureResponse("compile", compile_result.message, request_json, compile_result.diagnostics,
                            buildExtraFields(pipeline_object, workspace_revision))};
  }

  fs::create_directories(single_output_dir);
  const std::string test_list_path = (fs::path(single_output_dir) / "audio_studio_test_list.txt").string();
  const std::string test_list =
      "ac_run --endpoint as_datalink --mtu 512\n"
      "trace on\n"
      "pipeinstall " + compile_result.tplg_path + "\n";
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
  validation_request.as_server_path = resolveSimulatorRpcToolPath(
      root_dir_, "as_server", "AUDIO_STUDIO_VALIDATION_AS_SERVER_PATH");
  validation_request.as_log_path = resolveSimulatorRpcToolPath(
      root_dir_, "as_log", "AUDIO_STUDIO_VALIDATION_AS_LOG_PATH");
  validation_request.trace_ldc_path = resolveTraceLdcPath(root_dir_);

  auto validation_result = validation_runner_->run(validation_request);
  if (!validation_result.ok) {
    return {200, "application/json; charset=utf-8",
            failureResponse("validation", validation_result.message, request_json, validation_result.diagnostics,
                            buildExtraFields(pipeline_object, workspace_revision))};
  }

  {
    std::lock_guard<std::mutex> lk(mutex_);
    auto* found = findWorkspaceLocked(record.workspace_id);
    if (found) {
      found->dirty_pipeline_ids.erase(pipeline_id);
      found->validated_pipeline_ids.insert(pipeline_id);
      found->loaded_pipeline_ids.insert(pipeline_id);
      found->build_ok = found->dirty_pipeline_ids.empty() && !found->validated_pipeline_ids.empty();
      workspace_revision = found->workspace_revision;
    }
  }

  std::ostringstream os;
  os << "{\"ok\":true,\"status\":\"PIPE_LOADED\",\"runtime_state\":\"PIPE_LOADED\""
     << ",\"workspace_id\":\"" << jsonEscape(record.workspace_id) << "\""
     << ",\"pipeline_id\":\"" << jsonEscape(pipeline_id) << "\""
     << ",\"tplg_path\":\"" << jsonEscape(compile_result.tplg_path) << "\""
     << ",\"test_list_path\":\"" << jsonEscape(test_list_path) << "\""
     << ",\"validation\":\"" << jsonEscape(validation_result.message) << "\""
     << ",\"updated_pipeline\":" << pipeline_object
     << ",\"workspace_revision\":" << workspace_revision
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

  const std::string all_json = readTextFile(record.input_path);
  const std::string snapshot = jsonMemberValue(request_json, "snapshot");
  const std::string snapshot_json = snapshot.empty() ? request_json : snapshot;
  std::string pipeline_id = jsonStringFieldAny(request_json, {"pipeline_id", "pipe_id", "group_id", "groupId"});
  std::string pipeline_object;
  if (!all_json.empty() && !snapshot_json.empty()) {
    std::string regenerated_pipelines;
    std::set<std::string> generated_node_keys;
    std::vector<std::string> diagnostics;
    if (regeneratePipelinesFromSnapshot(all_json, snapshot_json, regenerated_pipelines,
                                        generated_node_keys, diagnostics)) {
      pipeline_object = singlePipelineObjectFromArray(regenerated_pipelines);
      if (!pipeline_object.empty()) pipeline_id = pipelineIdentity(pipeline_object);
    }
  }
  if (pipeline_id.empty() || pipeline_id == "ALL") {
    return {200, "application/json; charset=utf-8",
            failureResponse("runtime", "pipeline_id is required to unload a pipeline", request_json)};
  }

  uint64_t workspace_revision = record.workspace_revision;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    auto* found = findWorkspaceLocked(record.workspace_id);
    if (found) {
      found->loaded_pipeline_ids.erase(pipeline_id);
      workspace_revision = found->workspace_revision;
    }
  }

  std::ostringstream os;
  os << "{\"ok\":true,\"status\":\"UNLOADED\",\"runtime_state\":\"NOT_READY\""
     << ",\"workspace_id\":\"" << jsonEscape(record.workspace_id) << "\""
     << ",\"pipeline_id\":\"" << jsonEscape(pipeline_id) << "\"";
  if (!pipeline_object.empty()) os << ",\"updated_pipeline\":" << pipeline_object;
  os << ",\"workspace_revision\":" << workspace_revision << "}";
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

GuiRuntimeEngine::GuiRuntimeEngine(std::shared_ptr<BuildOrchestrator> build_orchestrator)
  : build_orchestrator_(std::move(build_orchestrator)) {
  if (!build_orchestrator_) build_orchestrator_ = std::make_shared<BuildOrchestrator>(".");
  rng_.seed(static_cast<unsigned>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
}

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
  (void)session_id;
  running_.store(true);
  return "{\"ok\":true,\"running\":true,\"runtime_state\":\"RUNNING\"}";
}

std::string GuiRuntimeEngine::stop(const std::string& session_id) {
  (void)session_id;
  running_.store(false);
  return "{\"ok\":true,\"running\":false,\"runtime_state\":\"NOT_READY\"}";
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
