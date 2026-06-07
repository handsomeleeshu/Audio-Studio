#include "audio_studio.hpp"
#include <iostream>

int main(int argc, char** argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  const int port = argc > 2 ? std::stoi(argv[2]) : 8080;
  auto runtime = std::make_shared<audiostudio::MockRuntimeEngine>();
  audiostudio::HttpServer server(root, port, runtime, runtime, runtime);
  try { return server.run(); }
  catch (const std::exception& e) { std::cerr << "server error: " << e.what() << std::endl; return 1; }
}
