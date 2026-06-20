#include <cassert>
#include <iostream>

#include "cli_common.hpp"

int main() {
  using audio_studio::cli::Args;
  using audio_studio::cli::jsonEscape;
  using audio_studio::cli::okJson;

  Args args({"--target", "dummy", "--action", "get-health"});
  assert(args.has("--target"));
  assert(args.valueAfter("--target") == "dummy");
  assert(args.valueAfter("--missing", "fallback") == "fallback");
  assert(jsonEscape("\"x\"") == "\\\"x\\\"");
  assert(okJson("as_control", "get-health").find("\"tool\":\"as_control\"") != std::string::npos);
  audio_studio::cli::CliLogEntry entry;
  entry.sequence = 7;
  entry.level = "error";
  entry.tag = "FW";
  entry.text = "audio controller failed";
  const std::string color = audio_studio::cli::formatLogEntry(entry, true);
  assert(color.find("\033[31m") != std::string::npos);
  assert(color.find("[ERR]") != std::string::npos);
  assert(color.find("[FW] audio controller failed") != std::string::npos);
  const std::string plain = audio_studio::cli::formatLogEntry(entry, false);
  assert(plain.find("\033[") == std::string::npos);
  assert(plain.find("#7 [ERR] [FW] audio controller failed") != std::string::npos);

  std::cout << "cli_tests passed\n";
  return 0;
}
