#pragma once

#include <map>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "log_device.hpp"
#include "status.hpp"

struct audio_studio_sof_logger_decoder;

namespace audio_studio::framework::log {

struct LogEntry {
  int sequence = 0;
  std::string level;
  std::string tag;
  std::string message;
  std::string text;
};

struct LogSessionConfig {
  std::string session_id;
  std::string driver_factory = "linux-host";
  std::string source = "firmware";
  std::string min_level = "debug";
  std::map<std::string, std::string> options;
  bool raw = false;
};

struct LogSessionInfo {
  std::string session_id;
  std::string driver_factory;
  std::string source;
  std::string min_level;
  bool running = false;
  size_t entries_read = 0;
  size_t raw_chunks_read = 0;
};

struct LogSessionStats {
  size_t entries_read = 0;
  size_t raw_chunks_read = 0;
  bool running = false;
};

class LogService {
public:
  using EntryInterceptor = std::function<bool(const LogEntry&)>;

  ~LogService();

  void configureDeviceRegistry(drivers::log::LogDeviceRegistry* registry);
  void setDefaultSessionConfig(LogSessionConfig config);
  void setEntryInterceptor(EntryInterceptor interceptor);

  framework::Status createSession(LogSessionConfig config, LogSessionInfo& out);
  framework::Status configureSession(const std::string& id, const LogSessionConfig& config, LogSessionInfo& out);
  framework::Status start(const std::string& id);
  framework::Status stop(const std::string& id);
  framework::Status closeSession(const std::string& id);
  framework::Status getSession(const std::string& id, LogSessionInfo& out) const;
  framework::Status getStats(const std::string& id, LogSessionStats& out) const;
  framework::Status readRaw(const std::string& id, size_t max_chunks, std::vector<drivers::log::LogRawChunk>& chunks);
  framework::Status readEntries(const std::string& id, size_t max_entries, std::vector<LogEntry>& entries);

  framework::Status append(std::string level, std::string message);
  std::vector<LogEntry> tail(size_t count) const;
  void clear();
  size_t size() const;

private:
  struct Session {
    LogSessionConfig config;
    std::unique_ptr<drivers::log::ILogDevice> device;
    bool running = false;
    size_t entries_read = 0;
    size_t raw_chunks_read = 0;
    std::string raw_trace_path;
    std::string decoded_trace_path;
    bool raw_trace_owned = false;
    bool decoded_trace_owned = false;
    std::string text_pending;
    std::string sof_decoded_pending;
    std::vector<uint8_t> sof_raw_pending;
    audio_studio_sof_logger_decoder* sof_decoder = nullptr;
    bool mirror_entries = false;
    size_t mirror_entry_cursor = 0;
  };

  static bool sofLoggerEnabled(const Session& session);
  LogSessionConfig mergeDefaultConfig(LogSessionConfig config) const;
  static std::string optionString(const LogSessionConfig& config, const std::string& key);
  static bool optionBool(const LogSessionConfig& config, const std::string& key);
  static std::string safePathToken(const std::string& value);
  static LogEntry decodeLine(int sequence, const std::string& line);
  static LogEntry decodeSofLoggerLine(int sequence, const std::string& line);
  static bool passesLevel(const std::string& level, const std::string& min_level);
  static void destroySofDecoder(Session& session);
  bool shouldMirrorEntriesLocked(const LogSessionConfig& config) const;
  framework::Status prepareTraceFiles(Session& session, bool truncate);
  framework::Status ensureSofDecoder(Session& session);
  framework::Status readRawLocked(const std::string& id, size_t max_chunks, std::vector<drivers::log::LogRawChunk>& chunks);
  framework::Status readMirrorEntriesLocked(Session& session, size_t max_entries, std::vector<LogEntry>& entries);
  framework::Status appendRawTrace(Session& session, const std::vector<drivers::log::LogRawChunk>& chunks);
  framework::Status decodeSofTrace(Session& session, size_t max_entries, std::vector<LogEntry>& entries);
  void parseTextLines(Session& session, const std::string& text, size_t max_entries, std::vector<LogEntry>& entries);
  void parseSofLoggerText(Session& session, const std::string& text, size_t max_entries, std::vector<LogEntry>& entries);
  void appendEntry(LogEntry entry);
  bool shouldIntercept(const LogEntry& entry) const;
  framework::Status requireSession(const std::string& id, Session*& session);
  framework::Status requireSession(const std::string& id, const Session*& session) const;
  LogSessionInfo infoFor(const Session& session) const;

  mutable std::mutex mutex_;
  int next_sequence_ = 1;
  std::vector<LogEntry> entries_;
  EntryInterceptor entry_interceptor_;
  drivers::log::LogDeviceRegistry* registry_ = nullptr;
  LogSessionConfig default_config_;
  std::map<std::string, Session> sessions_;
};

} // namespace audio_studio::framework::log
