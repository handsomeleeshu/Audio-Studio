#include <iostream>
#include <string>

#ifndef AUDIO_STUDIO_CLI_TOOL_NAME
#define AUDIO_STUDIO_CLI_TOOL_NAME "as_tool"
#endif

namespace {

void print_usage() {
  std::cout
      << AUDIO_STUDIO_CLI_TOOL_NAME << " M0 skeleton\n"
      << "\n"
      << "Usage:\n"
      << "  " << AUDIO_STUDIO_CLI_TOOL_NAME << " [--help] [--version] [--m0-check]\n"
      << "\n"
      << "This is a lightweight CLI front-end placeholder. CLI tools will remain\n"
      << "thin JSON-RPC clients and will not link directly against server framework,\n"
      << "platform drivers, or Audio Controller protocol code.\n";
}

} // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i] ? argv[i] : "";
    if (arg == "--help" || arg == "-h") {
      print_usage();
      return 0;
    }
    if (arg == "--version") {
      std::cout << AUDIO_STUDIO_CLI_TOOL_NAME << " 0.1.0-m0\n";
      return 0;
    }
    if (arg == "--m0-check") {
      std::cout << AUDIO_STUDIO_CLI_TOOL_NAME << ": M0 skeleton OK\n";
      return 0;
    }
    std::cerr << AUDIO_STUDIO_CLI_TOOL_NAME << ": unknown argument: " << arg << "\n";
    print_usage();
    return 2;
  }

  std::cout << AUDIO_STUDIO_CLI_TOOL_NAME << ": M0 skeleton ready. Use --help for details.\n";
  return 0;
}
