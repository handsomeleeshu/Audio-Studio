#include "audio_studio/framework/dump/dump_service.hpp"

#include <utility>

namespace audio_studio::framework::dump {

framework::Status DumpService::start(std::string id, std::string source) {
  if (id.empty()) return framework::Status::invalidArgument("dump id is empty");
  if (source.empty()) return framework::Status::invalidArgument("dump source is empty");
  if (sessions_.find(id) != sessions_.end()) return framework::Status::invalidArgument("dump session already exists: " + id);
  DumpSession session{id, std::move(source), true, 0};
  sessions_.emplace(session.id, std::move(session));
  return framework::Status::success();
}

framework::Status DumpService::write(const std::string& id, size_t bytes) {
  auto it = sessions_.find(id);
  if (it == sessions_.end()) return framework::Status::unavailable("dump session not found: " + id);
  if (!it->second.active) return framework::Status::unavailable("dump session is not active: " + id);
  it->second.bytes_written += bytes;
  return framework::Status::success();
}

framework::Status DumpService::stop(const std::string& id) {
  auto it = sessions_.find(id);
  if (it == sessions_.end()) return framework::Status::unavailable("dump session not found: " + id);
  it->second.active = false;
  return framework::Status::success();
}

framework::Status DumpService::get(const std::string& id, DumpSession& out) const {
  const auto it = sessions_.find(id);
  if (it == sessions_.end()) return framework::Status::unavailable("dump session not found: " + id);
  out = it->second;
  return framework::Status::success();
}

std::vector<DumpSession> DumpService::list() const {
  std::vector<DumpSession> out;
  out.reserve(sessions_.size());
  for (const auto& item : sessions_) out.push_back(item.second);
  return out;
}

} // namespace audio_studio::framework::dump
