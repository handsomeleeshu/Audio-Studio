#pragma once

#include <map>
#include <string>
#include <vector>

#include "status.hpp"

namespace audio_studio::framework::control {

struct ControlValue {
  std::string node_id;
  std::string parameter_id;
  std::string value;
};

class ControlService {
public:
  framework::Status set(ControlValue value);
  framework::Status get(const std::string& node_id, const std::string& parameter_id, ControlValue& out) const;
  std::vector<ControlValue> list() const;
  size_t size() const;

private:
  static std::string key(const std::string& node_id, const std::string& parameter_id);

  std::map<std::string, ControlValue> values_;
};

} // namespace audio_studio::framework::control
