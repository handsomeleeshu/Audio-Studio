#include "log_service.hpp"
#include "sof_logger_decoder_c.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
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

bool containsToken(const std::string& line, const std::string& token) {
  return line.find(token) != std::string::npos;
}

bool startsWith(const std::string& line, const std::string& prefix) {
  return line.rfind(prefix, 0) == 0;
}

bool isSofLoggerDiagnosticLine(const std::string& line) {
  return containsToken(line, "Skipped ") ||
         containsToken(line, "Potential mailbox wrap") ||
         startsWith(line, "Found valid LDC address") ||
         containsToken(line, "Seeking forward 4 bytes") ||
         containsToken(line, "Re-opening trace input file") ||
         containsToken(line, "negative DELTA");
}

bool isSofLoggerEntryLine(const std::string& line) {
  if (line.empty()) return false;
  if (isSofLoggerDiagnosticLine(line)) return false;
  if (containsToken(line, " TIMESTAMP") && containsToken(line, "CONTENT")) return false;
  return containsToken(line, " ERROR ") ||
         containsToken(line, " ERR ") ||
         containsToken(line, " WARNING ") ||
         containsToken(line, " WARN ") ||
         containsToken(line, " INFO ") ||
         containsToken(line, " DEBUG ") ||
         containsToken(line, " VERBOSE ") ||
         containsToken(line, " UNKNOWN ");
}

} // namespace

void LogService::configureDeviceRegistry(drivers::log::LogDeviceRegistry* registry) {
  registry_ = registry;
}

void LogService::setDefaultSessionConfig(LogSessionConfig config) {
  config.session_id.clear();
  if (!config.min_level.empty()) config.min_level = normalizeLevel(config.min_level);
  default_config_ = std::move(config);
}

void LogService::setEntryInterceptor(EntryInterceptor interceptor) {
  entry_interceptor_ = std::move(interceptor);
}

framework::Status LogService::createSession(LogSessionConfig config, LogSessionInfo& out) {
  out = {};
  config = mergeDefaultConfig(std::move(config));
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
  const auto safe_id = safePathToken(session.config.session_id);
  session.raw_trace_path = optionString(session.config, "raw_trace_path");
  if (session.raw_trace_path.empty()) session.raw_trace_path = "/tmp/audio_studio_" + safe_id + ".trace.raw";
  session.decoded_trace_path = optionString(session.config, "decoded_trace_path");
  if (session.decoded_trace_path.empty()) session.decoded_trace_path = "/tmp/audio_studio_" + safe_id + ".trace.log";
  if (sofLoggerEnabled(session)) {
    std::ofstream raw(session.raw_trace_path, std::ios::binary | std::ios::trunc);
    std::ofstream decoded(session.decoded_trace_path, std::ios::binary | std::ios::trunc);
  }
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
  if (!config.options.empty()) {
    session->raw_trace_path = optionString(session->config, "raw_trace_path");
    if (session->raw_trace_path.empty()) {
      session->raw_trace_path = "/tmp/audio_studio_" + safePathToken(session->config.session_id) + ".trace.raw";
    }
    session->decoded_trace_path = optionString(session->config, "decoded_trace_path");
    if (session->decoded_trace_path.empty()) {
      session->decoded_trace_path = "/tmp/audio_studio_" + safePathToken(session->config.session_id) + ".trace.log";
    }
    session->decoded_entries_read = 0;
  }
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
  if (sofLoggerEnabled(*session)) {
    if (!session->raw_trace_path.empty()) std::remove(session->raw_trace_path.c_str());
    if (!session->decoded_trace_path.empty()) std::remove(session->decoded_trace_path.c_str());
  }
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
    if (chunk.bytes.empty()) break;
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
    if (chunks.empty()) break;
    for (const auto& chunk : chunks) {
      if (sofLoggerEnabled(*session)) {
        status = appendRawTrace(*session, chunks);
        if (!status.ok()) return status;
        status = decodeSofTrace(*session, max_entries, entries);
        if (!status.ok()) return status;
        break;
      } else {
        const std::string text(chunk.bytes.begin(), chunk.bytes.end());
        std::istringstream lines(text);
        std::string line;
        while (entries.size() < max_entries && std::getline(lines, line)) {
          if (line.empty()) continue;
          auto entry = decodeLine(next_sequence_++, line);
          if (!passesLevel(entry.level, session->config.min_level)) continue;
          if (shouldIntercept(entry)) continue;
          entries.push_back(entry);
          entries_.push_back(entry);
          ++session->entries_read;
        }
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
  if (shouldIntercept(entry)) return framework::Status::success();
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

bool LogService::sofLoggerEnabled(const Session& session) {
  return !optionString(session.config, "trace_ldc").empty();
}

LogSessionConfig LogService::mergeDefaultConfig(LogSessionConfig config) const {
  LogSessionConfig merged = default_config_;
  if (!config.session_id.empty()) merged.session_id = std::move(config.session_id);
  if (!config.driver_factory.empty()) merged.driver_factory = std::move(config.driver_factory);
  if (!config.source.empty()) merged.source = std::move(config.source);
  if (!config.min_level.empty()) merged.min_level = std::move(config.min_level);
  merged.raw = config.raw;
  for (auto& item : config.options) merged.options[item.first] = std::move(item.second);

  if (merged.driver_factory.empty()) merged.driver_factory = "linux-host";
  if (merged.source.empty()) merged.source = "firmware";
  if (merged.min_level.empty()) merged.min_level = "debug";
  return merged;
}

std::string LogService::optionString(const LogSessionConfig& config, const std::string& key) {
  const auto it = config.options.find(key);
  return it == config.options.end() ? std::string() : it->second;
}

std::string LogService::safePathToken(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (const auto ch : value) {
    if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-') out.push_back(ch);
    else out.push_back('_');
  }
  return out.empty() ? "log" : out;
}

LogEntry LogService::decodeSofLoggerLine(int sequence, const std::string& line) {
  LogEntry entry;
  entry.sequence = sequence;
  entry.level = "info";
  if (containsToken(line, " ERROR ") || containsToken(line, " ERR ")) entry.level = "error";
  else if (containsToken(line, " WARNING ") || containsToken(line, " WARN ")) entry.level = "warning";
  else if (containsToken(line, " DEBUG ")) entry.level = "debug";
  else if (containsToken(line, " VERBOSE ")) entry.level = "verbose";
  entry.tag = "SOF";
  entry.message = line;
  entry.text = line;
  return entry;
}

framework::Status LogService::appendRawTrace(Session& session, const std::vector<drivers::log::LogRawChunk>& chunks) {
  std::ofstream raw(session.raw_trace_path, std::ios::binary | std::ios::app);
  if (!raw) return framework::Status::unavailable("failed to open SOF raw trace file: " + session.raw_trace_path);
  for (const auto& chunk : chunks) {
    if (!chunk.bytes.empty()) {
      raw.write(reinterpret_cast<const char*>(chunk.bytes.data()),
                static_cast<std::streamsize>(chunk.bytes.size()));
    }
  }
  if (!raw) return framework::Status::unavailable("failed to append SOF raw trace file: " + session.raw_trace_path);
  return framework::Status::success();
}

framework::Status LogService::decodeSofTrace(Session& session, size_t max_entries, std::vector<LogEntry>& entries) {
  const auto trace_ldc = optionString(session.config, "trace_ldc");
  const int rc = audio_studio_sof_logger_decode_file(
      session.raw_trace_path.c_str(), trace_ldc.c_str(),
      session.decoded_trace_path.c_str());
  if (rc != 0) return framework::Status::unavailable("SOF logger decoder failed to decode trace");

  std::ifstream decoded(session.decoded_trace_path);
  if (!decoded) return framework::Status::unavailable("failed to open SOF decoded trace file: " + session.decoded_trace_path);

  std::string line;
  size_t entry_index = 0;
  while (entries.size() < max_entries && std::getline(decoded, line)) {
    if (!isSofLoggerEntryLine(line)) continue;
    if (entry_index++ < session.decoded_entries_read) continue;
    auto entry = decodeSofLoggerLine(next_sequence_++, line);
    if (!passesLevel(entry.level, session.config.min_level)) continue;
    if (shouldIntercept(entry)) continue;
    entries.push_back(entry);
    entries_.push_back(entry);
    ++session.entries_read;
  }
  session.decoded_entries_read = entry_index;
  return framework::Status::success();
}

bool LogService::passesLevel(const std::string& level, const std::string& min_level) {
  return levelRank(normalizeLevel(level)) <= levelRank(normalizeLevel(min_level));
}

bool LogService::shouldIntercept(const LogEntry& entry) const {
  return entry_interceptor_ && entry_interceptor_(entry);
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
