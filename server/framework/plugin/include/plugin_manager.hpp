#pragma once

#include <map>
#include <string>
#include <vector>

#include "status.hpp"

namespace audio_studio::framework::plugin {

struct PluginDescriptor {
  std::string id;
  std::string name;
  std::string version;
  std::string provider;
  std::vector<std::string> capabilities;
  bool active = false;
};

class PluginManager {
public:
  framework::Status registerPlugin(PluginDescriptor descriptor);
  framework::Status unregisterPlugin(const std::string& id);
  framework::Status setActive(const std::string& id, bool active);
  framework::Status get(const std::string& id, PluginDescriptor& out) const;
  std::vector<PluginDescriptor> list() const;
  std::vector<PluginDescriptor> findByCapability(const std::string& capability) const;
  size_t size() const;

private:
  std::map<std::string, PluginDescriptor> plugins_;
};

} // namespace audio_studio::framework::plugin
