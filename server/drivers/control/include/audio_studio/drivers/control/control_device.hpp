#pragma once

#include <map>
#include <string>
#include <vector>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::drivers::control {

struct ControlInfo {
  std::string id;
  std::string name;
  bool readable = true;
  bool writable = true;
};

struct ControlStats {
  size_t writes = 0;
  size_t reads = 0;
  bool open = false;
};

class ControlDevice {
public:
  framework::Status open(std::string profile);
  framework::Status setValue(std::string id, std::string value);
  framework::Status getValue(const std::string& id, std::string& out);
  std::vector<ControlInfo> listControls() const;
  ControlStats stats() const;
  void close();

private:
  std::string profile_;
  bool open_ = false;
  size_t writes_ = 0;
  size_t reads_ = 0;
  std::map<std::string, std::string> values_;
};

} // namespace audio_studio::drivers::control
