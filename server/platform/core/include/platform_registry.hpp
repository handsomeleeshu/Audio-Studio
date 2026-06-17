#pragma once

#include <map>
#include <string>
#include <vector>

#include "status.hpp"

namespace audio_studio::platform::core {

struct PlatformProfile {
  std::string id;
  std::string name;
  std::string transport;
  std::vector<std::string> capabilities;
  bool available = false;
};

class PlatformRegistry {
public:
  framework::Status registerPlatform(PlatformProfile profile);
  framework::Status getPlatform(const std::string& id, PlatformProfile& out) const;
  std::vector<PlatformProfile> listPlatforms() const;
  std::vector<PlatformProfile> findByCapability(const std::string& capability) const;
  bool hasPlatform(const std::string& id) const;
  size_t size() const;

private:
  std::map<std::string, PlatformProfile> platforms_;
};

} // namespace audio_studio::platform::core
