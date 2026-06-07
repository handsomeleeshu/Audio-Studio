#include "audio_studio.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

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

std::map<std::string, std::string> parseQuery(const std::string& q) {
  std::map<std::string, std::string> out;
  std::string key, val;
  enum { KEY, VAL } mode = KEY;
  for (size_t i = 0; i <= q.size(); ++i) {
    char c = (i == q.size()) ? '&' : q[i];
    if (c == '&') { if (!key.empty()) out[key] = val; key.clear(); val.clear(); mode = KEY; }
    else if (c == '=' && mode == KEY) mode = VAL;
    else if (mode == KEY) key.push_back(c);
    else val.push_back(c);
  }
  return out;
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

HttpServer::HttpServer(std::string root_dir, int port, std::shared_ptr<IRuntimeEngine> runtime,
                       std::shared_ptr<INodeController> node_controller,
                       std::shared_ptr<IParameterController> parameter_controller)
  : root_dir_(std::move(root_dir)), port_(port), runtime_(std::move(runtime)),
    node_controller_(std::move(node_controller)), parameter_controller_(std::move(parameter_controller)) {}

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
  if (req.method == "OPTIONS") return {204, "text/plain", ""};
  if (req.method == "GET" && (req.path == "/" || req.path == "/index.html" || req.path == "/frontend/" || req.path == "/frontend/index.html")) return serveFile("frontend/index.html", "text/html; charset=utf-8");
  if (req.method == "GET" && req.path.rfind("/assets/", 0) == 0) return serveFile("frontend" + req.path, "");
  if (req.method == "GET" && (req.path == "/config/A2.json" || req.path == "/api/config")) return serveFile("config/A2.json", "application/json; charset=utf-8");
  if (req.method == "GET" && req.path == "/api/telemetry") {
    auto q = parseQuery(req.query);
    return {200, "application/json", runtime_->telemetry(splitCsv(q["nodes"]))};
  }
  if (req.method == "POST" && req.path == "/api/pipeline/validate") return {200, "application/json", runtime_->validatePipeline(req.body)};
  if (req.method == "POST" && req.path == "/api/pipeline/build") return {200, "application/json", runtime_->buildPipeline(req.body)};
  if (req.method == "POST" && req.path == "/api/pipeline/edit") return {200, "application/json", runtime_->pipelineEditEvent(req.body)};
  if (req.method == "POST" && req.path == "/api/pipeline/tool") return {200, "application/json", runtime_->pipelineToolAction(req.body)};
  if (req.method == "POST" && req.path == "/api/project/save") return {200, "application/json", runtime_->pipelineEditEvent(std::string("{\"action\":\"project_save\",\"payload\":") + req.body + "}")};
  if (req.method == "POST" && req.path == "/api/runtime/run") return {200, "application/json", runtime_->run(req.body)};
  if (req.method == "POST" && req.path == "/api/runtime/stop") return {200, "application/json", runtime_->stop(req.body)};
  if (req.method == "POST" && req.path == "/api/param/update") return {200, "application/json", parameter_controller_->updateParameter(req.body)};
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
