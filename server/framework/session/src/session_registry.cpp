#include "audio_studio/framework/session/session_registry.hpp"

#include <utility>

namespace audio_studio::framework::session {

framework::Status SessionRegistry::create(std::string id, std::string owner) {
  if (id.empty()) return framework::Status::invalidArgument("session id is empty");
  if (owner.empty()) return framework::Status::invalidArgument("session owner is empty");
  if (sessions_.find(id) != sessions_.end()) return framework::Status::invalidArgument("session already exists: " + id);
  SessionInfo info{id, std::move(owner), true};
  sessions_.emplace(info.id, std::move(info));
  return framework::Status::success();
}

framework::Status SessionRegistry::close(const std::string& id) {
  const auto it = sessions_.find(id);
  if (it == sessions_.end()) return framework::Status::unavailable("session not found: " + id);
  it->second.active = false;
  return framework::Status::success();
}

bool SessionRegistry::has(const std::string& id) const {
  return sessions_.find(id) != sessions_.end();
}

SessionInfo SessionRegistry::get(const std::string& id) const {
  const auto it = sessions_.find(id);
  return it == sessions_.end() ? SessionInfo{} : it->second;
}

std::vector<SessionInfo> SessionRegistry::list() const {
  std::vector<SessionInfo> out;
  out.reserve(sessions_.size());
  for (const auto& item : sessions_) out.push_back(item.second);
  return out;
}

size_t SessionRegistry::activeCount() const {
  size_t count = 0;
  for (const auto& item : sessions_) {
    if (item.second.active) ++count;
  }
  return count;
}

} // namespace audio_studio::framework::session
