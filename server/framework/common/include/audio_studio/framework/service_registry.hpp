#pragma once

#include <map>
#include <string>
#include <vector>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::framework {

class ServiceRegistry {
public:
  Status registerService(std::string name, std::string description);
  bool hasService(const std::string& name) const;
  std::vector<std::string> serviceNames() const;
  std::string describe(const std::string& name) const;

private:
  std::map<std::string, std::string> services_;
};

} // namespace audio_studio::framework
