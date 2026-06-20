#include "log_service.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace audio_studio::framework::log {
namespace {

int levelRank(const std::string& level) {
  if (level == "critical") return 0;
  if (level == "error" || level == "err") return 1;
  if (level == "warning" || level == "warn") return 2;
  if (level == "info") return 3;
  if (level == "debug") return 4;
  if (level == "verbose") return 5;
  return 3;
}

std::string normalizeLevel(std::string level) {
  std::transform(level.begin(), level.end(), level.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (level == "err") return "error";
  if (level == "warn") return "warning";
  if (level.empty()) return "info";
  return level;
}

} // namespace

void LogService::configureDeviceRegistry(drivers::log::LogDeviceRegistry* registry) {
  registry_ = registry;
}

framework::Status LogService::createSession(LogSessionConfig config, LogSessionInfo& out) {
  out = {};
  if (config.session_id.empty()) {
    config.session_id = "log_" + std::to_string(static_cast<long long>(sessions_.size() + 1));
  }
  if (config.driver_factory.empty()) return framework::Status::invalidArgument("log driver factory is empty");
  if (config.source.empty()) return framework::Status::invalidArgument("log source is empty");
  if (sessions_.find(config.session_id) != sessions_.end()) {
    return framework::Status::invalidArgument("log session already exists: " + config.session_id);
  }

  std::unique_ptr<drivers::log::ILogDevice> device;
  if (registry_ != nullptr) {
    drivers::log::LogDeviceConfig device_config;
    device_config.source = config.source;
    device_config.options = config.options;
    device = registry_->create(config.driver_factory, device_config);
    if (!device) return framework::Status::unavailable("log device factory not registered: " + config.driver_factory);
  }

  Session session;
  session.config = std::move(config);
  session.config.min_level = normalizeLevel(session.config.min_level);
  session.device = std::move(device);
  auto inserted = sessions_.emplace(session.config.session_id, std::move(session));
  out = infoFor(inserted.first->second);
  return framework::Status::success();
}

framework::Status LogService::configureSession(const std::string& id, const LogSessionConfig& config, LogSessionInfo& out) {
  Session* session = nullptr;
  auto status = requireSession(id, session);
  if (!status.ok()) return status;
  if (session->running) return framework::Status::unavailable("cannot configure running log session: " + id);
  session->config.min_level = normalizeLevel(config.min_level.empty() ? session->config.min_level : config.min_level);
  session->config.raw = config.raw;
  if (!config.options.empty()) session->config.options = config.options;
  if ((!config.source.empty() && config.source != session->config.source) || !config.options.empty()) {
    if (!config.source.empty()) session->config.source = config.source;
    if (session->device) {
      drivers::log::LogDeviceConfig device_config;
      device_config.source = session->config.source;
      device_config.options = session->config.options;
      status = session->device->configure(device_config);
      if (!status.ok()) return status;
    }
  }
  out = infoFor(*session);
  return framework::Status::success();
}

framework::Status LogService::start(const std::string& id) {
  Session* session = nullptr;
  auto status = requireSession(id, session);
  if (!status.ok()) return status;
  if (session->device) {
    status = session->device->start();
    if (!status.ok()) return status;
  }
  session->running = true;
  return framework::Status::success();
}

framework::Status LogService::stop(const std::string& id) {
  Session* session = nullptr;
  auto status = requireSession(id, session);
  if (!status.ok()) return status;
  if (session->device) {
    status = session->device->stop();
    if (!status.ok()) return status;
  }
  session->running = false;
  return framework::Status::success();
}

framework::Status LogService::closeSession(const std::string& id) {
  Session* session = nullptr;
  auto status = requireSession(id, session);
  if (!status.ok()) return status;
  if (session->device) session->device->close();
  sessions_.erase(id);
  return framework::Status::success();
}

framework::Status LogService::getSession(const std::string& id, LogSessionInfo& out) const {
  const Session* session = nullptr;
  auto status = requireSession(id, session);
  if (!status.ok()) return status;
  out = infoFor(*session);
  return framework::Status::success();
}

framework::Status LogService::getStats(const std::string& id, LogSessionStats& out) const {
  const Session* session = nullptr;
  auto status = requireSession(id, session);
  if (!status.ok()) return status;
  out.entries_read = session->entries_read;
  out.raw_chunks_read = session->raw_chunks_read;
  out.running = session->running;
  return framework::Status::success();
}

framework::Status LogService::readRaw(const std::string& id,
                                      size_t max_chunks,
                                      std::vector<drivers::log::LogRawChunk>& chunks) {
  chunks.clear();
  Session* session = nullptr;
  auto status = requireSession(id, session);
  if (!status.ok()) return status;
  if (!session->running) return framework::Status::unavailable("log session is not running: " + id);
  if (max_chunks == 0) return framework::Status::invalidArgument("log read max_chunks is zero");

  if (!session->device) return framework::Status::unavailable("log session has no device: " + id);
  for (size_t i = 0; i < max_chunks; ++i) {
    drivers::log::LogRawChunk chunk;
    status = session->device->readChunk(chunk, 100);
    if (!status.ok()) {
      if (chunks.empty()) return status;
      break;
    }
    chunks.push_back(std::move(chunk));
    ++session->raw_chunks_read;
  }
  return framework::Status::success();
}

framework::Status LogService::readEntries(const std::string& id, size_t max_entries, std::vector<LogEntry>& entries) {
  entries.clear();
  if (max_entries == 0) return framework::Status::invalidArgument("log read max_entries is zero");
  Session* session = nullptr;
  auto status = requireSession(id, session);
  if (!status.ok()) return status;

  while (entries.size() < max_entries) {
    std::vector<drivers::log::LogRawChunk> chunks;
    status = readRaw(id, 1, chunks);
    if (!status.ok()) {
      if (entries.empty()) return status;
      break;
    }
    for (const auto& chunk : chunks) {
      const std::string text(chunk.bytes.begin(), chunk.bytes.end());
      std::istringstream lines(text);
      std::string line;
      while (entries.size() < max_entries && std::getline(lines, line)) {
        if (line.empty()) continue;
        auto entry = decodeLine(next_sequence_++, line);
        if (!passesLevel(entry.level, session->config.min_level)) continue;
        entries.push_back(entry);
        entries_.push_back(entry);
        ++session->entries_read;
      }
    }
  }
  return framework::Status::success();
}

framework::Status LogService::append(std::string level, std::string message) {
  if (level.empty()) return framework::Status::invalidArgument("log level is empty");
  if (message.empty()) return framework::Status::invalidArgument("log message is empty");
  const auto normalized = normalizeLevel(std::move(level));
  LogEntry entry;
  entry.sequence = next_sequence_++;
  entry.level = normalized;
  entry.tag = "APP";
  entry.message = std::move(message);
  entry.text = entry.message;
  entries_.push_back(std::move(entry));
  return framework::Status::success();
}

std::vector<LogEntry> LogService::tail(size_t count) const {
  const size_t start = count >= entries_.size() ? 0 : entries_.size() - count;
  return std::vector<LogEntry>(entries_.begin() + static_cast<long>(start), entries_.end());
}

void LogService::clear() {
  entries_.clear();
}

size_t LogService::size() const {
  return entries_.size();
}

LogEntry LogService::decodeLine(int sequence, const std::string& line) {
  LogEntry entry;
  entry.sequence = sequence;
  const auto first = line.find('|');
  const auto second = first == std::string::npos ? std::string::npos : line.find('|', first + 1);
  if (first != std::string::npos && second != std::string::npos) {
    entry.level = normalizeLevel(line.substr(0, first));
    entry.tag = line.substr(first + 1, second - first - 1);
    entry.message = line.substr(second + 1);
  } else {
    entry.level = "info";
    entry.tag = "FW";
    entry.message = line;
  }
  if (entry.tag.empty()) entry.tag = "FW";
  entry.text = entry.message;
  return entry;
}

bool LogService::passesLevel(const std::string& level, const std::string& min_level) {
  return levelRank(normalizeLevel(level)) <= levelRank(normalizeLevel(min_level));
}

framework::Status LogService::requireSession(const std::string& id, Session*& session) {
  const auto it = sessions_.find(id);
  if (it == sessions_.end()) return framework::Status::unavailable("log session not found: " + id);
  session = &it->second;
  return framework::Status::success();
}

framework::Status LogService::requireSession(const std::string& id, const Session*& session) const {
  const auto it = sessions_.find(id);
  if (it == sessions_.end()) return framework::Status::unavailable("log session not found: " + id);
  session = &it->second;
  return framework::Status::success();
}

LogSessionInfo LogService::infoFor(const Session& session) const {
  LogSessionInfo info;
  info.session_id = session.config.session_id;
  info.driver_factory = session.config.driver_factory;
  info.source = session.config.source;
  info.min_level = session.config.min_level;
  info.running = session.running;
  info.entries_read = session.entries_read;
  info.raw_chunks_read = session.raw_chunks_read;
  return info;
}

} // namespace audio_studio::framework::log
