#pragma once

#include <map>
#include <string>
#include <vector>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::framework::session {

struct SessionInfo {
  std::string id;
  std::string owner;
  bool active = false;
};

class SessionRegistry {
public:
  framework::Status create(std::string id, std::string owner);
  framework::Status close(const std::string& id);
  bool has(const std::string& id) const;
  SessionInfo get(const std::string& id) const;
  std::vector<SessionInfo> list() const;
  size_t activeCount() const;

private:
  std::map<std::string, SessionInfo> sessions_;
};

} // namespace audio_studio::framework::session
