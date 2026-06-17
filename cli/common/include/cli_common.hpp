#pragma once

#include <string>
#include <vector>

namespace audio_studio::cli {

class Args {
public:
  Args(int argc, char** argv);
  explicit Args(std::vector<std::string> values);

  bool has(const std::string& flag) const;
  std::string valueAfter(const std::string& flag, const std::string& fallback = "") const;
  const std::vector<std::string>& values() const;

private:
  std::vector<std::string> values_;
};

std::string jsonEscape(const std::string& input);
std::string okJson(const std::string& tool, const std::string& detail);
std::string usageText(const std::string& tool, const std::string& action);
int runDummyTool(const std::string& tool, const std::string& action, const Args& args);
int runCliTool(const std::string& tool, const std::string& action, const Args& args);
int runCliTool(const std::string& tool, const std::string& default_action, int argc, char** argv);

} // namespace audio_studio::cli
