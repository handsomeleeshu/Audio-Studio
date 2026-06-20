#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "log_device.hpp"
#include "status.hpp"

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
  void configureDeviceRegistry(drivers::log::LogDeviceRegistry* registry);

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
  };

  static LogEntry decodeLine(int sequence, const std::string& line);
  static bool passesLevel(const std::string& level, const std::string& min_level);
  framework::Status requireSession(const std::string& id, Session*& session);
  framework::Status requireSession(const std::string& id, const Session*& session) const;
  LogSessionInfo infoFor(const Session& session) const;

  int next_sequence_ = 1;
  std::vector<LogEntry> entries_;
  drivers::log::LogDeviceRegistry* registry_ = nullptr;
  std::map<std::string, Session> sessions_;
};

} // namespace audio_studio::framework::log
