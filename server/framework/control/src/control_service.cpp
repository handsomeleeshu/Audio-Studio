#include "audio_studio/framework/control/control_service.hpp"

#include <utility>

namespace audio_studio::framework::control {

framework::Status ControlService::set(ControlValue value) {
  if (value.node_id.empty()) return framework::Status::invalidArgument("control node id is empty");
  if (value.parameter_id.empty()) return framework::Status::invalidArgument("control parameter id is empty");
  values_[key(value.node_id, value.parameter_id)] = std::move(value);
  return framework::Status::success();
}

framework::Status ControlService::get(const std::string& node_id, const std::string& parameter_id, ControlValue& out) const {
  const auto it = values_.find(key(node_id, parameter_id));
  if (it == values_.end()) return framework::Status::unavailable("control value not found");
  out = it->second;
  return framework::Status::success();
}

std::vector<ControlValue> ControlService::list() const {
  std::vector<ControlValue> out;
  out.reserve(values_.size());
  for (const auto& item : values_) out.push_back(item.second);
  return out;
}

size_t ControlService::size() const {
  return values_.size();
}

std::string ControlService::key(const std::string& node_id, const std::string& parameter_id) {
  return node_id + ":" + parameter_id;
}

} // namespace audio_studio::framework::control
