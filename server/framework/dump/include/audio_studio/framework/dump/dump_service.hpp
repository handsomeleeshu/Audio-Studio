#pragma once

#include <map>
#include <string>
#include <vector>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::framework::dump {

struct DumpSession {
  std::string id;
  std::string source;
  bool active = false;
  size_t bytes_written = 0;
};

class DumpService {
public:
  framework::Status start(std::string id, std::string source);
  framework::Status write(const std::string& id, size_t bytes);
  framework::Status stop(const std::string& id);
  framework::Status get(const std::string& id, DumpSession& out) const;
  std::vector<DumpSession> list() const;

private:
  std::map<std::string, DumpSession> sessions_;
};

} // namespace audio_studio::framework::dump
