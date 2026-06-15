#include "audio_studio/framework/plugin/plugin_manager.hpp"

#include <algorithm>
#include <utility>

namespace audio_studio::framework::plugin {

framework::Status PluginManager::registerPlugin(PluginDescriptor descriptor) {
  if (descriptor.id.empty()) return framework::Status::invalidArgument("plugin id is empty");
  if (descriptor.name.empty()) return framework::Status::invalidArgument("plugin name is empty");
  if (plugins_.find(descriptor.id) != plugins_.end()) return framework::Status::invalidArgument("plugin already registered: " + descriptor.id);
  plugins_.emplace(descriptor.id, std::move(descriptor));
  return framework::Status::success();
}

framework::Status PluginManager::unregisterPlugin(const std::string& id) {
  const auto erased = plugins_.erase(id);
  if (erased == 0) return framework::Status::unavailable("plugin not found: " + id);
  return framework::Status::success();
}

framework::Status PluginManager::setActive(const std::string& id, bool active) {
  auto it = plugins_.find(id);
  if (it == plugins_.end()) return framework::Status::unavailable("plugin not found: " + id);
  it->second.active = active;
  return framework::Status::success();
}

framework::Status PluginManager::get(const std::string& id, PluginDescriptor& out) const {
  const auto it = plugins_.find(id);
  if (it == plugins_.end()) return framework::Status::unavailable("plugin not found: " + id);
  out = it->second;
  return framework::Status::success();
}

std::vector<PluginDescriptor> PluginManager::list() const {
  std::vector<PluginDescriptor> out;
  out.reserve(plugins_.size());
  for (const auto& item : plugins_) out.push_back(item.second);
  return out;
}

std::vector<PluginDescriptor> PluginManager::findByCapability(const std::string& capability) const {
  std::vector<PluginDescriptor> out;
  for (const auto& item : plugins_) {
    const auto& capabilities = item.second.capabilities;
    if (std::find(capabilities.begin(), capabilities.end(), capability) != capabilities.end()) {
      out.push_back(item.second);
    }
  }
  return out;
}

size_t PluginManager::size() const {
  return plugins_.size();
}

} // namespace audio_studio::framework::plugin
