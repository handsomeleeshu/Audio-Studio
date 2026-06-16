#include <iostream>
#include <string>

#include "autoconfig.h"

namespace {

const char* toolOs() {
#if defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#else
  return "linux";
#endif
}

const char* targetPlatform() {
#if defined(CONFIG_TARGET_PLATFORM_SIMULATOR)
  return "simulator";
#else
  return "a2";
#endif
}

} // namespace

int main(int argc, char** argv) {
  const std::string arg = argc > 1 ? argv[1] : "--version";
  if (arg == "--version") {
    std::cout << "Audio Studio as_server initial " << toolOs() << "/" << targetPlatform() << "\n";
    return 0;
  }
  if (arg == "--health") {
    std::cout << "{\"ok\":true,\"tool_os\":\"" << toolOs() << "\",\"platform\":\"" << targetPlatform()
              << "\"}\n";
    return 0;
  }
  std::cerr << "usage: as_server [--version|--health]\n";
  return 2;
}
