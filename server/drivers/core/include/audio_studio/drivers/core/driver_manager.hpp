#pragma once

#include <map>
#include <string>
#include <vector>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::drivers::core {

struct DriverInfo {
  std::string category;
  std::string name;
  std::string detail;
  bool active = false;
};

class DriverManager {
public:
  framework::Status registerDriver(DriverInfo info);
  framework::Status unregisterDriver(const std::string& category, const std::string& name);
  framework::Status setActive(const std::string& category, const std::string& name, bool active);
  framework::Status getDriver(const std::string& category, const std::string& name, DriverInfo& out) const;
  bool hasDriver(const std::string& category, const std::string& name) const;
  std::vector<DriverInfo> listDrivers() const;
  std::vector<DriverInfo> listByCategory(const std::string& category) const;
  size_t size() const;

private:
  static std::string key(const std::string& category, const std::string& name);

  std::map<std::string, DriverInfo> drivers_;
};

} // namespace audio_studio::drivers::core
