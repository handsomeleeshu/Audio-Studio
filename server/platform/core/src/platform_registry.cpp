#include "audio_studio/platform/core/platform_registry.hpp"

#include <algorithm>
#include <utility>

namespace audio_studio::platform::core {

framework::Status PlatformRegistry::registerPlatform(PlatformProfile profile) {
  if (profile.id.empty()) return framework::Status::invalidArgument("platform id is empty");
  if (profile.name.empty()) return framework::Status::invalidArgument("platform name is empty");
  if (platforms_.find(profile.id) != platforms_.end()) return framework::Status::invalidArgument("platform already registered: " + profile.id);
  platforms_.emplace(profile.id, std::move(profile));
  return framework::Status::success();
}

framework::Status PlatformRegistry::getPlatform(const std::string& id, PlatformProfile& out) const {
  const auto it = platforms_.find(id);
  if (it == platforms_.end()) return framework::Status::unavailable("platform not found: " + id);
  out = it->second;
  return framework::Status::success();
}

std::vector<PlatformProfile> PlatformRegistry::listPlatforms() const {
  std::vector<PlatformProfile> out;
  out.reserve(platforms_.size());
  for (const auto& item : platforms_) out.push_back(item.second);
  return out;
}

std::vector<PlatformProfile> PlatformRegistry::findByCapability(const std::string& capability) const {
  std::vector<PlatformProfile> out;
  for (const auto& item : platforms_) {
    const auto& capabilities = item.second.capabilities;
    if (std::find(capabilities.begin(), capabilities.end(), capability) != capabilities.end()) {
      out.push_back(item.second);
    }
  }
  return out;
}

bool PlatformRegistry::hasPlatform(const std::string& id) const {
  return platforms_.find(id) != platforms_.end();
}

size_t PlatformRegistry::size() const {
  return platforms_.size();
}

} // namespace audio_studio::platform::core
