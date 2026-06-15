#include "audio_studio/drivers/core/driver_manager.hpp"

#include <utility>

namespace audio_studio::drivers::core {

framework::Status DriverManager::registerDriver(DriverInfo info) {
  if (info.category.empty()) return framework::Status::invalidArgument("driver category is empty");
  if (info.name.empty()) return framework::Status::invalidArgument("driver name is empty");
  const auto driver_key = key(info.category, info.name);
  if (drivers_.find(driver_key) != drivers_.end()) return framework::Status::invalidArgument("driver already registered: " + driver_key);
  drivers_.emplace(driver_key, std::move(info));
  return framework::Status::success();
}

framework::Status DriverManager::unregisterDriver(const std::string& category, const std::string& name) {
  const auto erased = drivers_.erase(key(category, name));
  if (erased == 0) return framework::Status::unavailable("driver not found: " + key(category, name));
  return framework::Status::success();
}

framework::Status DriverManager::setActive(const std::string& category, const std::string& name, bool active) {
  auto it = drivers_.find(key(category, name));
  if (it == drivers_.end()) return framework::Status::unavailable("driver not found: " + key(category, name));
  it->second.active = active;
  return framework::Status::success();
}

framework::Status DriverManager::getDriver(const std::string& category, const std::string& name, DriverInfo& out) const {
  const auto it = drivers_.find(key(category, name));
  if (it == drivers_.end()) return framework::Status::unavailable("driver not found: " + key(category, name));
  out = it->second;
  return framework::Status::success();
}

bool DriverManager::hasDriver(const std::string& category, const std::string& name) const {
  return drivers_.find(key(category, name)) != drivers_.end();
}

std::vector<DriverInfo> DriverManager::listDrivers() const {
  std::vector<DriverInfo> out;
  out.reserve(drivers_.size());
  for (const auto& item : drivers_) out.push_back(item.second);
  return out;
}

std::vector<DriverInfo> DriverManager::listByCategory(const std::string& category) const {
  std::vector<DriverInfo> out;
  for (const auto& item : drivers_) {
    if (item.second.category == category) out.push_back(item.second);
  }
  return out;
}

size_t DriverManager::size() const {
  return drivers_.size();
}

std::string DriverManager::key(const std::string& category, const std::string& name) {
  return category + ":" + name;
}

} // namespace audio_studio::drivers::core
