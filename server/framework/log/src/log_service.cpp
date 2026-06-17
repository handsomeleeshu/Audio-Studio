#include "log_service.hpp"

#include <algorithm>
#include <utility>

namespace audio_studio::framework::log {

framework::Status LogService::append(std::string level, std::string message) {
  if (level.empty()) return framework::Status::invalidArgument("log level is empty");
  if (message.empty()) return framework::Status::invalidArgument("log message is empty");
  entries_.push_back({next_sequence_++, std::move(level), std::move(message)});
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

} // namespace audio_studio::framework::log
