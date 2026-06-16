#include "audio_studio.hpp"
#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

#ifdef AUDIO_STUDIO_GUI_BACKEND_RPC
#include "json_rpc.hpp"
#endif

namespace audiostudio {

std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream ss; ss << in.rdbuf(); return ss.str();
}

std::string jsonEscape(const std::string& s) {
  std::string out; out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': break;
      case '\t': out += "\\t"; break;
      default: out += c; break;
    }
  }
  return out;
}


static std::string jsonStringField(const std::string& body, const std::string& field, const std::string& fallback = "") {
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
    char c = body[i];
    if (esc) { out.push_back(c); esc = false; continue; }
    if (c == '\\') { esc = true; continue; }
    if (c == '"') return out.empty() ? fallback : out;
    out.push_back(c);
  }
  return fallback;
}

std::vector<std::string> splitCsv(const std::string& csv) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : csv) {
    if (c == ',') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
    else cur.push_back(c);
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

static int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static std::string urlDecode(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '+') { out.push_back(' '); continue; }
    if (s[i] == '%' && i + 2 < s.size()) {
      const int hi = hexValue(s[i + 1]);
      const int lo = hexValue(s[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(s[i]);
  }
  return out;
}

std::map<std::string, std::string> parseQuery(const std::string& q) {
  std::map<std::string, std::string> out;
  std::string key, val;
  enum { KEY, VAL } mode = KEY;
  for (size_t i = 0; i <= q.size(); ++i) {
    char c = (i == q.size()) ? '&' : q[i];
    if (c == '&') {
      if (!key.empty()) out[urlDecode(key)] = urlDecode(val);
      key.clear();
      val.clear();
      mode = KEY;
    }
    else if (c == '=' && mode == KEY) mode = VAL;
    else if (mode == KEY) key.push_back(c);
    else val.push_back(c);
  }
  return out;
}


std::string sanitizeConfigFileName(const std::string& input) {
  if (input.empty()) return "A2.json";
  std::string out;
  for (char c : input) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    if (ok) out.push_back(c);
  }
  if (out.empty()) out = "A2.json";
  if (out.size() < 5 || out.substr(out.size() - 5) != ".json") out += ".json";
  return out;
}

std::vector<std::string> listConfigJsonFiles(const std::string& root_dir) {
  std::vector<std::string> files;
  const std::string dir_path = root_dir + "/config";
  DIR* dir = opendir(dir_path.c_str());
  if (!dir) return files;
  while (auto* ent = readdir(dir)) {
    std::string name = ent->d_name;
    if (name.size() >= 5 && name.substr(name.size() - 5) == ".json" && name != "built-in-algorithm.json" && name != "projects.json") files.push_back(name);
  }
  closedir(dir);
  std::sort(files.begin(), files.end());
  return files;
}

std::string configProjectListJson(const std::string& root_dir) {
  auto files = listConfigJsonFiles(root_dir);
  std::ostringstream ss;
  ss << "{\"projects\":[";
  for (size_t i = 0; i < files.size(); ++i) {
    std::string name = files[i];
    if (name.size() >= 5) name = name.substr(0, name.size() - 5);
    if (i) ss << ",";
    ss << "{\"file\":\"" << jsonEscape(files[i]) << "\",\"name\":\"" << jsonEscape(name) << "\"}";
  }
  ss << "]}";
  return ss.str();
}

std::string contentTypeForPath(const std::string& path) {
  if (path.size() >= 5 && path.substr(path.size()-5) == ".html") return "text/html; charset=utf-8";
  if (path.size() >= 4 && path.substr(path.size()-4) == ".css") return "text/css; charset=utf-8";
  if (path.size() >= 3 && path.substr(path.size()-3) == ".js") return "text/javascript; charset=utf-8";
  if (path.size() >= 5 && path.substr(path.size()-5) == ".json") return "application/json; charset=utf-8";
  if (path.size() >= 4 && path.substr(path.size()-4) == ".png") return "image/png";
  return "application/octet-stream";
}

static void logApiRequest(const HttpRequest& req) {
  if (req.path.rfind("/api/", 0) != 0) return;
  std::cout << "\n[AudioStudio HTTP API] " << req.method << " " << req.path;
  if (!req.query.empty()) std::cout << "?" << req.query;
  if (!req.body.empty()) {
    std::cout << "\n  Body: " << req.body.substr(0, 1200)
              << (req.body.size() > 1200 ? "...(truncated)" : "");
  }
  std::cout << "\n" << std::flush;
}

static bool startsWith(const std::string& value, const std::string& prefix) {
  return value.rfind(prefix, 0) == 0;
}

static bool hasJsonSuffix(const std::string& path) {
  return path.size() >= 5 && path.substr(path.size() - 5) == ".json";
}

static bool isFrontendIndexPath(const std::string& path) {
  return path == "/" || path == "/index.html" ||
         path == "/GUI/frontend/" || path == "/GUI/frontend/index.html" ||
         path == "/frontend/" || path == "/frontend/index.html";
}

#ifdef AUDIO_STUDIO_GUI_BACKEND_RPC
static std::string handleBackendRpc(const std::string& body) {
  audio_studio::rpc::JsonRpcEndpoint endpoint;
  endpoint.addMethod("server.health", [](const audio_studio::rpc::JsonValue&) {
    audio_studio::rpc::JsonValue result = audio_studio::rpc::JsonValue::object();
    result["ok"] = true;
    result["tool_os"] = "linux";
    result["platform"] = "gui_backend";
    result["transport"] = "http";
    return result;
  });
  endpoint.addMethod("gui.ping", [](const audio_studio::rpc::JsonValue&) {
    audio_studio::rpc::JsonValue result = audio_studio::rpc::JsonValue::object();
    result["ok"] = true;
    result["service"] = "gui_backend";
    return result;
  });
  return endpoint.handleRequest(body);
}
#endif

HttpServer::HttpServer(std::string root_dir, int port, std::shared_ptr<IRuntimeEngine> runtime,
                       std::shared_ptr<INodeController> node_controller,
                       std::shared_ptr<IParameterController> parameter_controller,
                       std::shared_ptr<ITargetConfigController> target_config_controller,
                       std::shared_ptr<IInspectorController> inspector_controller,
                       std::shared_ptr<IAlgorithmCostController> algorithm_cost_controller,
                       std::shared_ptr<IDspCoreLoadingController> dsp_core_loading_controller,
                        std::shared_ptr<IEventLogController> event_log_controller,
                        std::shared_ptr<ISystemHealthController> system_health_controller,
                        std::shared_ptr<IAudioIoController> audio_io_controller,
                        std::shared_ptr<IRealTimeProbeController> real_time_probe_controller)
  : root_dir_(std::move(root_dir)), port_(port), runtime_(std::move(runtime)),
    node_controller_(std::move(node_controller)), parameter_controller_(std::move(parameter_controller)),
    target_config_controller_(std::move(target_config_controller)),
    inspector_controller_(std::move(inspector_controller)),
    algorithm_cost_controller_(std::move(algorithm_cost_controller)),
    dsp_core_loading_controller_(std::move(dsp_core_loading_controller)),
    event_log_controller_(std::move(event_log_controller)),
    system_health_controller_(std::move(system_health_controller)),
    audio_io_controller_(std::move(audio_io_controller)),
    real_time_probe_controller_(std::move(real_time_probe_controller)) {}

HttpResponse HttpServer::serveFile(const std::string& rel_path, const std::string& content_type) {
  HttpResponse res;
  std::string path = root_dir_ + "/" + rel_path;
  auto body = readFile(path);
  if (body.empty()) {
    res.status = 404; res.content_type = "text/plain"; res.body = "Not found: " + rel_path; return res;
  }
  res.content_type = content_type.empty() ? contentTypeForPath(rel_path) : content_type;
  res.body = std::move(body);
  return res;
}

HttpResponse HttpServer::handle(const HttpRequest& req) {
  logApiRequest(req);
  static size_t ui_notification_seq = 0;
  static std::vector<std::string> ui_notifications;
  const std::string frontend_root = "GUI/frontend";
  const std::string gui_asset_prefix = "/GUI/frontend/assets/";
  const std::string legacy_frontend_prefix = "/frontend/";
  const std::string gui_config_prefix = "/GUI/frontend/config/";
  const std::string legacy_config_prefix = "/frontend/config/";
  auto serveFrontendFile = [&](const std::string& rel_path, const std::string& content_type = "") {
    return serveFile(frontend_root + "/" + rel_path, content_type);
  };

  if (req.method == "OPTIONS") return {204, "text/plain", ""};
#ifdef AUDIO_STUDIO_GUI_BACKEND_RPC
  if (req.method == "POST" && req.path == "/rpc") {
    return {200, "application/json; charset=utf-8", handleBackendRpc(req.body)};
  }
#endif
  if (req.method == "GET" && isFrontendIndexPath(req.path)) return serveFrontendFile("index.html", "text/html; charset=utf-8");
  if (req.method == "GET" && startsWith(req.path, "/assets/")) return serveFrontendFile(req.path.substr(1), "");
  if (req.method == "GET" && startsWith(req.path, gui_asset_prefix)) return serveFile(req.path.substr(1), "");
  if (req.method == "GET" && startsWith(req.path, legacy_frontend_prefix) && startsWith(req.path.substr(legacy_frontend_prefix.size()), "assets/")) {
    return serveFrontendFile(req.path.substr(legacy_frontend_prefix.size()), "");
  }
  if (req.method == "GET" && startsWith(req.path, gui_config_prefix) && hasJsonSuffix(req.path)) {
    return serveFrontendFile(std::string("config/") + sanitizeConfigFileName(req.path.substr(gui_config_prefix.size())), "application/json; charset=utf-8");
  }
  if (req.method == "GET" && startsWith(req.path, legacy_config_prefix) && hasJsonSuffix(req.path)) {
    return serveFrontendFile(std::string("config/") + sanitizeConfigFileName(req.path.substr(legacy_config_prefix.size())), "application/json; charset=utf-8");
  }
  if (req.method == "GET" && req.path == "/api/projects") {
    return {200, "application/json; charset=utf-8", configProjectListJson(root_dir_)};
  }
  if (req.method == "GET" && req.path == "/api/config") {
    auto q = parseQuery(req.query);
    return serveFile(std::string("config/") + sanitizeConfigFileName(q["project"]), "application/json; charset=utf-8");
  }
  if (req.method == "GET" && req.path.rfind("/config/", 0) == 0 &&
      req.path.size() >= 12 && req.path.substr(req.path.size() - 5) == ".json") {
    return serveFile(std::string("config/") + sanitizeConfigFileName(req.path.substr(8)), "application/json; charset=utf-8");
  }
  if (req.method == "GET" && req.path == "/api/target/config") {
    auto q = parseQuery(req.query);
    if (!target_config_controller_) return {503, "application/json", R"({"ok":false,"error":"target config controller not configured"})"};
    return {200, "application/json", target_config_controller_->targetConfig(q)};
  }
  if (req.method == "POST" && req.path == "/api/target/config") {
    if (!target_config_controller_) return {503, "application/json", R"({"ok":false,"error":"target config controller not configured"})"};
    return {200, "application/json", target_config_controller_->updateTargetConfig(req.body)};
  }
  if (req.method == "GET" && req.path == "/api/telemetry") {
    auto q = parseQuery(req.query);
    return {200, "application/json", runtime_->telemetry(splitCsv(q["nodes"]))};
  }
  if (req.method == "GET" && req.path == "/api/inspector/live") {
    auto q = parseQuery(req.query);
    if (!inspector_controller_) return {503, "application/json", R"({"ok":false,"error":"inspector controller not configured"})"};
    return {200, "application/json", inspector_controller_->liveData(q)};
  }
  if (req.method == "GET" && req.path == "/api/algorithm/cost/live") {
    auto q = parseQuery(req.query);
    if (!algorithm_cost_controller_) return {503, "application/json", R"({"ok":false,"error":"algorithm cost controller not configured"})"};
    return {200, "application/json", algorithm_cost_controller_->liveCosts(q)};
  }
  if (req.method == "GET" && req.path == "/api/dsp/core/loading") {
    auto q = parseQuery(req.query);
    if (!dsp_core_loading_controller_) return {503, "application/json", R"({"ok":false,"error":"dsp core loading controller not configured"})"};
    return {200, "application/json", dsp_core_loading_controller_->liveCoreLoading(q)};
  }
  if (req.method == "GET" && req.path == "/api/event-log/live") {
    auto q = parseQuery(req.query);
    if (!event_log_controller_) return {503, "application/json", R"({"ok":false,"error":"event log controller not configured"})"};
    return {200, "application/json", event_log_controller_->liveEvents(q)};
  }
  if (req.method == "GET" && req.path == "/api/system/health/live") {
    auto q = parseQuery(req.query);
    if (!system_health_controller_) return {503, "application/json", R"({"ok":false,"error":"system health controller not configured"})"};
    return {200, "application/json", system_health_controller_->liveHealth(q)};
  }
  if (req.method == "GET" && req.path == "/api/audio/io/live") {
    auto q = parseQuery(req.query);
    if (!audio_io_controller_) return {503, "application/json", R"({"ok":false,"error":"audio io controller not configured"})"};
    return {200, "application/json", audio_io_controller_->liveAudioIo(q)};
  }
  if (req.method == "GET" && req.path == "/api/realtime/probe/live") {
    auto q = parseQuery(req.query);
    if (!real_time_probe_controller_) return {503, "application/json", R"({"ok":false,"error":"realtime probe controller not configured"})"};
    return {200, "application/json", real_time_probe_controller_->liveProbeData(q)};
  }
  if (req.method == "POST" && req.path == "/api/realtime/probe/config") {
    if (!real_time_probe_controller_) return {503, "application/json", R"({"ok":false,"error":"realtime probe controller not configured"})"};
    return {200, "application/json", real_time_probe_controller_->configureProbe(req.body)};
  }
  if (req.method == "GET" && req.path == "/api/inspector/buffer/live") {
    auto q = parseQuery(req.query);
    if (!inspector_controller_) return {503, "application/json", R"({"ok":false,"error":"inspector controller not configured"})"};
    return {200, "application/json", inspector_controller_->bufferLiveData(q)};
  }
  if (req.method == "POST" && req.path == "/api/pipeline/validate") return {200, "application/json", runtime_->validatePipeline(req.body)};
  if (req.method == "POST" && req.path == "/api/pipeline/build") return {200, "application/json", runtime_->buildPipeline(req.body)};
  if (req.method == "POST" && req.path == "/api/pipeline/edit") return {200, "application/json", runtime_->pipelineEditEvent(req.body)};
  if (req.method == "POST" && req.path == "/api/pipeline/tool") return {200, "application/json", runtime_->pipelineToolAction(req.body)};
  if (req.method == "POST" && req.path == "/api/ui/event") {
    if (!event_log_controller_) return {200, "application/json", runtime_->pipelineToolAction(req.body)};
    return {200, "application/json", event_log_controller_->postEvent(req.body)};
  }
  if (req.method == "GET" && req.path == "/api/ui/notify") {
    std::ostringstream ss;
    ss << "{\"ok\":true,\"messages\":[";
    for (size_t i = 0; i < ui_notifications.size(); ++i) {
      if (i) ss << ",";
      ss << ui_notifications[i];
    }
    ss << "]}";
    ui_notifications.clear();
    return {200, "application/json", ss.str()};
  }
  if (req.method == "POST" && req.path == "/api/ui/notify") {
    const std::string level = jsonStringField(req.body, "level", jsonStringField(req.body, "kind", "info"));
    const std::string message = jsonStringField(req.body, "message", jsonStringField(req.body, "msg", req.body.empty() ? "Backend notification" : req.body));
    std::ostringstream item;
    item << "{\"id\":" << (++ui_notification_seq)
         << ",\"level\":\"" << jsonEscape(level) << "\""
         << ",\"message\":\"" << jsonEscape(message) << "\"}";
    ui_notifications.push_back(item.str());
    if (ui_notifications.size() > 40) ui_notifications.erase(ui_notifications.begin(), ui_notifications.begin() + static_cast<long>(ui_notifications.size() - 40));
    return {200, "application/json", std::string("{\"ok\":true,\"id\":") + std::to_string(ui_notification_seq) + "}"};
  }
  if (req.method == "POST" && req.path == "/api/project/save") return {200, "application/json", runtime_->pipelineEditEvent(std::string("{\"action\":\"project_save\",\"payload\":") + req.body + "}")};
  if (req.method == "GET" && req.path == "/api/runtime/buffer/formats/live") {
    auto q = parseQuery(req.query);
    const std::string running_value = q["running"];
    const bool run = running_value == "1" || running_value == "true" || running_value == "running";
    auto edges = splitCsv(q["edges"]);
    const int sample_rate = 48000;
    const int channels = 2;
    const int bits = 16;
    const int frame_samples = 480;
    std::ostringstream ss;
    ss << "{\"ok\":true,\"source\":\"backend\",\"running\":" << (run ? "true" : "false") << ",\"formats\":[";
    for (size_t i = 0; i < edges.size(); ++i) {
      if (edges[i].empty()) continue;
      if (i) ss << ",";
      ss << "{\"edge_key\":\"" << jsonEscape(edges[i]) << "\""
         << ",\"source\":\"backend\""
         << ",\"channels\":" << channels
         << ",\"sampleRate\":" << sample_rate
         << ",\"bits\":" << bits
         << ",\"frameSamples\":" << frame_samples
         << ",\"label\":\"2ch · 48 kHz · 16-bit\"}";
    }
    ss << "]}";
    return {200, "application/json", ss.str()};
  }
  if (req.method == "POST" && req.path == "/api/runtime/run") return {200, "application/json", runtime_->run(req.body)};
  if (req.method == "POST" && req.path == "/api/runtime/stop") return {200, "application/json", runtime_->stop(req.body)};
  if (req.method == "POST" && req.path == "/api/param/update") return {200, "application/json", parameter_controller_->updateParameter(req.body)};
  if (req.method == "POST" && req.path == "/api/inspector/inspect") {
    if (!inspector_controller_) return {503, "application/json", R"({"ok":false,"error":"inspector controller not configured"})"};
    return {200, "application/json", inspector_controller_->inspectNode(req.body)};
  }
  if (req.method == "POST" && req.path == "/api/inspector/buffer/inspect") {
    if (!inspector_controller_) return {503, "application/json", R"({"ok":false,"error":"inspector controller not configured"})"};
    return {200, "application/json", inspector_controller_->inspectBuffer(req.body)};
  }
  if (req.method == "POST" && req.path == "/api/node/action") return {200, "application/json", node_controller_->onNodeAction(req.body)};
  return {404, "application/json", "{\"ok\":false,\"error\":\"route not found\"}"};
}

static bool readRequest(int fd, HttpRequest& req) {
  std::string data;
  char buf[4096];
  ssize_t n;
  while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) {
    data.append(buf, buf + n);
    if (data.find("\r\n\r\n") != std::string::npos) break;
    if (data.size() > 1024 * 1024) return false;
  }
  if (data.empty()) return false;
  const auto header_end = data.find("\r\n\r\n");
  std::string headers = data.substr(0, header_end);
  std::istringstream hs(headers);
  std::string line;
  if (!std::getline(hs, line)) return false;
  if (!line.empty() && line.back() == '\r') line.pop_back();
  std::istringstream first(line);
  std::string target, version;
  first >> req.method >> target >> version;
  auto qpos = target.find('?');
  req.path = qpos == std::string::npos ? target : target.substr(0, qpos);
  req.query = qpos == std::string::npos ? "" : target.substr(qpos + 1);
  size_t content_length = 0;
  while (std::getline(hs, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    auto pos = line.find(':');
    if (pos != std::string::npos) {
      std::string key = line.substr(0, pos);
      std::string value = line.substr(pos + 1);
      while (!value.empty() && value.front() == ' ') value.erase(value.begin());
      req.headers[key] = value;
      std::string lower = key; std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
      if (lower == "content-length") content_length = static_cast<size_t>(std::stoul(value));
    }
  }
  req.body = header_end == std::string::npos ? "" : data.substr(header_end + 4);
  while (req.body.size() < content_length) {
    n = recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) break;
    req.body.append(buf, buf + n);
  }
  if (req.body.size() > content_length) req.body.resize(content_length);
  return true;
}

static void writeResponse(int fd, const HttpResponse& res) {
  std::ostringstream head;
  head << "HTTP/1.1 " << res.status << " OK\r\n";
  head << "Content-Type: " << res.content_type << "\r\n";
  head << "Content-Length: " << res.body.size() << "\r\n";
  head << "Access-Control-Allow-Origin: *\r\n";
  head << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
  head << "Access-Control-Allow-Headers: Content-Type\r\n";
  head << "Connection: close\r\n\r\n";
  const std::string hs = head.str();
  send(fd, hs.data(), hs.size(), 0);
  if (!res.body.empty()) send(fd, res.body.data(), res.body.size(), 0);
}

int HttpServer::run() {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) throw std::runtime_error("socket failed");
  int opt = 1; setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(static_cast<uint16_t>(port_));
  if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) throw std::runtime_error("bind failed");
  if (listen(server_fd, 32) < 0) throw std::runtime_error("listen failed");
  std::cout << "Audio Studio server: http://127.0.0.1:" << port_ << std::endl;
  std::cout << "Open this URL, not python -m http.server, to see backend callback prints." << std::endl;
  while (true) {
    int fd = accept(server_fd, nullptr, nullptr);
    if (fd < 0) continue;
    HttpRequest req;
    if (readRequest(fd, req)) {
      auto res = handle(req);
      writeResponse(fd, res);
    }
    close(fd);
  }
  return 0;
}

} // namespace audiostudio
