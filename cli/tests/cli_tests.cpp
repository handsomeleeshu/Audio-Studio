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

  std::cout << "cli_tests passed\n";
  return 0;
}
