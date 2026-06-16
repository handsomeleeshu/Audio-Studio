#include "audio_studio/cli/cli_common.hpp"

#include <iostream>
#include <sstream>

#include "dummy_driver.hpp"

namespace audio_studio::cli {

Args::Args(int argc, char** argv) {
  values_.reserve(argc > 0 ? static_cast<size_t>(argc - 1) : 0);
  for (int i = 1; i < argc; ++i) values_.push_back(argv[i]);
}

Args::Args(std::vector<std::string> values) : values_(std::move(values)) {}

bool Args::has(const std::string& flag) const {
  for (const auto& value : values_) {
    if (value == flag) return true;
  }
  return false;
}

std::string Args::valueAfter(const std::string& flag, const std::string& fallback) const {
  for (size_t i = 0; i + 1 < values_.size(); ++i) {
    if (values_[i] == flag) return values_[i + 1];
  }
  return fallback;
}

const std::vector<std::string>& Args::values() const {
  return values_;
}

std::string jsonEscape(const std::string& input) {
  std::string out;
  out.reserve(input.size() + 8);
  for (const char c : input) {
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else out.push_back(c);
  }
  return out;
}

std::string okJson(const std::string& tool, const std::string& detail) {
  std::ostringstream out;
  out << "{\"ok\":true,\"tool\":\"" << jsonEscape(tool)
      << "\",\"detail\":\"" << jsonEscape(detail) << "\"}";
  return out.str();
}

std::string usageText(const std::string& tool, const std::string& action) {
  return "usage: " + tool + " [--self-test|--target dummy] (" + action + ")";
}

int runDummyTool(const std::string& tool, const std::string& action, const Args& args) {
  if (args.has("--help")) {
    std::cout << usageText(tool, action) << "\n";
    return 0;
  }
  const std::string target = args.valueAfter("--target", "dummy");
  if (target != "dummy") {
    std::cerr << "{\"ok\":false,\"error\":\"only dummy target is available in host-alone mode\"}\n";
    return 2;
  }

  audio_studio::drivers::dummy::DummyDriver driver;
  auto status = driver.open();
  if (!status.ok()) {
    std::cerr << status.toJson() << "\n";
    return 1;
  }
  driver.start();
  driver.sendCommand(tool + ":" + action);
  driver.stop();

  std::cout << okJson(tool, action) << "\n";
  return 0;
}

} // namespace audio_studio::cli
