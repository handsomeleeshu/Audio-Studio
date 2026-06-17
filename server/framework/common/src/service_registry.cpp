#include "service_registry.hpp"

#include <utility>

namespace audio_studio::framework {

Status ServiceRegistry::registerService(std::string name, std::string description) {
  if (name.empty()) return Status::invalidArgument("service name is empty");
  if (services_.find(name) != services_.end()) return Status::invalidArgument("service already registered: " + name);
  services_.emplace(std::move(name), std::move(description));
  return Status::success();
}

bool ServiceRegistry::hasService(const std::string& name) const {
  return services_.find(name) != services_.end();
}

std::vector<std::string> ServiceRegistry::serviceNames() const {
  std::vector<std::string> names;
  names.reserve(services_.size());
  for (const auto& item : services_) names.push_back(item.first);
  return names;
}

std::string ServiceRegistry::describe(const std::string& name) const {
  const auto it = services_.find(name);
  return it == services_.end() ? std::string() : it->second;
}

} // namespace audio_studio::framework
