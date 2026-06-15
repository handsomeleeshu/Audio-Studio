#include "server_app.hpp"

#include <iostream>
#include <string>

namespace audiostudio::server {
namespace {

void print_usage() {
  std::cout
      << "Audio Studio formal as_server M0 skeleton\n"
      << "\n"
      << "Usage:\n"
      << "  as_server [--help] [--version] [--m0-check]\n"
      << "\n"
      << "This executable validates the top-level repository layout and build\n"
      << "boundaries for the formal Audio Studio Server.\n";
}

} // namespace

int ServerApp::run(int argc, char** argv) const {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i] ? argv[i] : "";
    if (arg == "--help" || arg == "-h") {
      print_usage();
      return 0;
    }
    if (arg == "--version") {
      std::cout << "as_server 0.1.0-m0\n";
      return 0;
    }
    if (arg == "--m0-check") {
      std::cout << "as_server: M0 skeleton OK\n";
      return 0;
    }
    std::cerr << "as_server: unknown argument: " << arg << "\n";
    print_usage();
    return 2;
  }

  std::cout << "as_server: M0 skeleton ready. Use --help for details.\n";
  return 0;
}

} // namespace audiostudio::server
