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

constexpr size_t kMaxStoredEntries = 65536;
constexpr size_t kSofTraceResyncBytes = sizeof(uint32_t);

std::mutex& sofLoggerDecodeMutex() {
  static std::mutex mutex;
  return mutex;
}

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

LogService::~LogService() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& item : sessions_) {
    if (item.second.device) item.second.device->close();
    destroySofDecoder(item.second);
  }
}

void LogService::configureDeviceRegistry(drivers::log::LogDeviceRegistry* registry) {
  std::lock_guard<std::mutex> lock(mutex_);
  registry_ = registry;
}

void LogService::setDefaultSessionConfig(LogSessionConfig config) {
  std::lock_guard<std::mutex> lock(mutex_);
  config.session_id.clear();
  if (!config.min_level.empty()) config.min_level = normalizeLevel(config.min_level);
  default_config_ = std::move(config);
}

void LogService::setEntryInterceptor(EntryInterceptor interceptor) {
  std::lock_guard<std::mutex> lock(mutex_);
  entry_interceptor_ = std::move(interceptor);
}

framework::Status LogService::createSession(LogSessionConfig config, LogSessionInfo& out) {
  std::lock_guard<std::mutex> lock(mutex_);
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
  if (sofLoggerEnabled(session)) {
    auto status = prepareTraceFiles(session, true);
    if (!status.ok()) return status;
  }
  session.device = std::move(device);
  auto inserted = sessions_.emplace(session.config.session_id, std::move(session));
  out = infoFor(inserted.first->second);
  return framework::Status::success();
}

framework::Status LogService::configureSession(const std::string& id, const LogSessionConfig& config, LogSessionInfo& out) {
  std::lock_guard<std::mutex> lock(mutex_);
  Session* session = nullptr;
  auto status = requireSession(id, session);
  if (!status.ok()) return status;
  if (session->running) return framework::Status::unavailable("cannot configure running log session: " + id);
  session->config.min_level = normalizeLevel(config.min_level.empty() ? session->config.min_level : config.min_level);
  session->config.raw = config.raw;
  const bool options_changed = !config.options.empty();
  if (options_changed) {
    for (const auto& item : config.options) session->config.options[item.first] = item.second;
  }
  if (!config.options.empty()) {
    destroySofDecoder(*session);
    session->sof_raw_pending.clear();
    session->sof_decoded_pending.clear();
    if (sofLoggerEnabled(*session)) {
      status = prepareTraceFiles(*session, true);
      if (!status.ok()) return status;
    }
  }
  if ((!config.source.empty() && config.source != session->config.source) || options_changed) {
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
  std::lock_guard<std::mutex> lock(mutex_);
  Session* session = nullptr;
  auto status = requireSession(id, session);
  if (!status.ok()) return status;
  if (sofLoggerEnabled(*session)) {
    status = ensureSofDecoder(*session);
    if (!status.ok()) return status;
  }
  if (session->device) {
    status = session->device->start();
    if (!status.ok()) return status;
  }
  session->running = true;
  return framework::Status::success();
}

framework::Status LogService::stop(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
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
  std::lock_guard<std::mutex> lock(mutex_);
  Session* session = nullptr;
  auto status = requireSession(id, session);
  if (!status.ok()) return status;
  if (session->device) session->device->close();
  destroySofDecoder(*session);
  if (sofLoggerEnabled(*session)) {
    if (session->raw_trace_owned && !session->raw_trace_path.empty()) std::remove(session->raw_trace_path.c_str());
    if (session->decoded_trace_owned && !session->decoded_trace_path.empty()) std::remove(session->decoded_trace_path.c_str());
  }
  sessions_.erase(id);
  return framework::Status::success();
}

framework::Status LogService::getSession(const std::string& id, LogSessionInfo& out) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const Session* session = nullptr;
  auto status = requireSession(id, session);
  if (!status.ok()) return status;
  out = infoFor(*session);
  return framework::Status::success();
}

framework::Status LogService::getStats(const std::string& id, LogSessionStats& out) const {
  std::lock_guard<std::mutex> lock(mutex_);
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
  std::lock_guard<std::mutex> lock(mutex_);
  return readRawLocked(id, max_chunks, chunks);
}

framework::Status LogService::readRawLocked(const std::string& id,
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
  std::lock_guard<std::mutex> lock(mutex_);
  entries.clear();
  if (max_entries == 0) return framework::Status::invalidArgument("log read max_entries is zero");
  Session* session = nullptr;
  auto status = requireSession(id, session);
  if (!status.ok()) return status;

  while (entries.size() < max_entries) {
    std::vector<drivers::log::LogRawChunk> chunks;
    status = readRawLocked(id, 1, chunks);
    if (!status.ok()) {
      if (!sofLoggerEnabled(*session) && !session->text_pending.empty()) {
        auto entry = decodeLine(next_sequence_++, session->text_pending);
        session->text_pending.clear();
        if (passesLevel(entry.level, session->config.min_level) && !shouldIntercept(entry)) {
          entries.push_back(entry);
          appendEntry(entry);
          ++session->entries_read;
        }
        return framework::Status::success();
      }
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
        parseTextLines(*session, text, max_entries, entries);
      }
    }
  }
  return framework::Status::success();
}

framework::Status LogService::append(std::string level, std::string message) {
  std::lock_guard<std::mutex> lock(mutex_);
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
  appendEntry(std::move(entry));
  return framework::Status::success();
}

std::vector<LogEntry> LogService::tail(size_t count) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const size_t start = count >= entries_.size() ? 0 : entries_.size() - count;
  return std::vector<LogEntry>(entries_.begin() + static_cast<long>(start), entries_.end());
}

void LogService::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.clear();
}

size_t LogService::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
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

void LogService::destroySofDecoder(Session& session) {
  if (session.sof_decoder) {
    audio_studio_sof_logger_decoder_destroy(session.sof_decoder);
    session.sof_decoder = nullptr;
  }
}

framework::Status LogService::prepareTraceFiles(Session& session, bool truncate) {
  const auto safe_id = safePathToken(session.config.session_id);
  session.raw_trace_path = optionString(session.config, "raw_trace_path");
  session.raw_trace_owned = session.raw_trace_path.empty();
  if (session.raw_trace_path.empty()) session.raw_trace_path = "/tmp/audio_studio_" + safe_id + ".trace.raw";
  session.decoded_trace_path = optionString(session.config, "decoded_trace_path");
  session.decoded_trace_owned = session.decoded_trace_path.empty();
  if (session.decoded_trace_path.empty()) session.decoded_trace_path = "/tmp/audio_studio_" + safe_id + ".trace.log";

  if (!truncate) return framework::Status::success();
  {
    std::ofstream raw(session.raw_trace_path, std::ios::binary | std::ios::trunc);
    if (!raw) return framework::Status::unavailable("failed to create SOF raw trace file: " + session.raw_trace_path);
  }
  {
    std::ofstream decoded(session.decoded_trace_path, std::ios::binary | std::ios::trunc);
    if (!decoded) return framework::Status::unavailable("failed to create SOF decoded trace file: " + session.decoded_trace_path);
  }
  return framework::Status::success();
}

framework::Status LogService::ensureSofDecoder(Session& session) {
  if (session.sof_decoder) return framework::Status::success();
  const auto trace_ldc = optionString(session.config, "trace_ldc");
  audio_studio_sof_logger_decoder* decoder = nullptr;
  const int rc = audio_studio_sof_logger_decoder_create(trace_ldc.c_str(), &decoder);
  if (rc != 0) {
    return framework::Status::unavailable("failed to open SOF logger dictionary: " + trace_ldc);
  }
  session.sof_decoder = decoder;
  return framework::Status::success();
}

framework::Status LogService::appendRawTrace(Session& session, const std::vector<drivers::log::LogRawChunk>& chunks) {
  std::ofstream raw;
  bool raw_open = false;
  if (!session.raw_trace_path.empty()) {
    raw.open(session.raw_trace_path, std::ios::binary | std::ios::app);
    if (!raw) return framework::Status::unavailable("failed to open SOF raw trace file: " + session.raw_trace_path);
    raw_open = true;
  }
  for (const auto& chunk : chunks) {
    if (!chunk.bytes.empty()) {
      session.sof_raw_pending.insert(session.sof_raw_pending.end(), chunk.bytes.begin(), chunk.bytes.end());
      if (raw) {
        raw.write(reinterpret_cast<const char*>(chunk.bytes.data()),
                  static_cast<std::streamsize>(chunk.bytes.size()));
      }
    }
  }
  if (raw_open && !raw) return framework::Status::unavailable("failed to append SOF raw trace file: " + session.raw_trace_path);
  return framework::Status::success();
}

framework::Status LogService::decodeSofTrace(Session& session, size_t max_entries, std::vector<LogEntry>& entries) {
  auto status = ensureSofDecoder(session);
  if (!status.ok()) return status;

  std::vector<uint8_t> complete;
  while (!session.sof_raw_pending.empty()) {
    unsigned long record_size = 0;
    const int rc = audio_studio_sof_logger_decoder_record_size(
        session.sof_decoder, session.sof_raw_pending.data(),
        static_cast<unsigned long>(session.sof_raw_pending.size()), &record_size);
    if (rc > 0) break;
    if (rc < 0 || record_size == 0ul) {
      const size_t drop = std::min(kSofTraceResyncBytes, session.sof_raw_pending.size());
      session.sof_raw_pending.erase(session.sof_raw_pending.begin(),
                                    session.sof_raw_pending.begin() + static_cast<long>(drop));
      continue;
    }
    complete.insert(complete.end(), session.sof_raw_pending.begin(),
                    session.sof_raw_pending.begin() + static_cast<long>(record_size));
    session.sof_raw_pending.erase(session.sof_raw_pending.begin(),
                                  session.sof_raw_pending.begin() + static_cast<long>(record_size));
  }
  if (complete.empty()) return framework::Status::success();

  char* decoded = nullptr;
  unsigned long decoded_size = 0;
  int rc;
  {
    std::lock_guard<std::mutex> decode_lock(sofLoggerDecodeMutex());
    rc = audio_studio_sof_logger_decoder_decode_buffer(
        session.sof_decoder, complete.data(), static_cast<unsigned long>(complete.size()),
        &decoded, &decoded_size);
  }
  if (rc != 0) {
    if (decoded) audio_studio_sof_logger_decoder_free_output(decoded);
    return framework::Status::unavailable("SOF logger decoder failed to decode trace");
  }
  const std::string text(decoded ? decoded : "", decoded ? decoded_size : 0ul);
  if (decoded) audio_studio_sof_logger_decoder_free_output(decoded);
  if (!session.decoded_trace_path.empty()) {
    std::ofstream out(session.decoded_trace_path, std::ios::binary | std::ios::app);
    if (!out) return framework::Status::unavailable("failed to open SOF decoded trace file: " + session.decoded_trace_path);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) return framework::Status::unavailable("failed to append SOF decoded trace file: " + session.decoded_trace_path);
  }
  parseSofLoggerText(session, text, max_entries, entries);
  return framework::Status::success();
}

void LogService::parseTextLines(Session& session, const std::string& text, size_t max_entries, std::vector<LogEntry>& entries) {
  session.text_pending += text;
  size_t line_start = 0;
  while (entries.size() < max_entries) {
    const auto newline = session.text_pending.find('\n', line_start);
    if (newline == std::string::npos) break;
    std::string line = session.text_pending.substr(line_start, newline - line_start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    line_start = newline + 1;
    if (line.empty()) continue;
    auto entry = decodeLine(next_sequence_++, line);
    if (!passesLevel(entry.level, session.config.min_level)) continue;
    if (shouldIntercept(entry)) continue;
    entries.push_back(entry);
    appendEntry(entry);
    ++session.entries_read;
  }
  if (line_start > 0) session.text_pending.erase(0, line_start);
}

void LogService::parseSofLoggerText(Session& session, const std::string& text, size_t max_entries, std::vector<LogEntry>& entries) {
  session.sof_decoded_pending += text;
  size_t line_start = 0;
  while (entries.size() < max_entries) {
    const auto newline = session.sof_decoded_pending.find('\n', line_start);
    if (newline == std::string::npos) break;
    std::string line = session.sof_decoded_pending.substr(line_start, newline - line_start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    line_start = newline + 1;
    if (!isSofLoggerEntryLine(line)) continue;
    auto entry = decodeSofLoggerLine(next_sequence_++, line);
    if (!passesLevel(entry.level, session.config.min_level)) continue;
    if (shouldIntercept(entry)) continue;
    entries.push_back(entry);
    appendEntry(entry);
    ++session.entries_read;
  }
  if (line_start > 0) session.sof_decoded_pending.erase(0, line_start);
}

void LogService::appendEntry(LogEntry entry) {
  entries_.push_back(std::move(entry));
  if (entries_.size() > kMaxStoredEntries) {
    const auto overflow = entries_.size() - kMaxStoredEntries;
    entries_.erase(entries_.begin(), entries_.begin() + static_cast<long>(overflow));
  }
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
