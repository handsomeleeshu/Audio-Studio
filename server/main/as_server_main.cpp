#include "server_app.hpp"

int main(int argc, char** argv) {
  return audiostudio::server::ServerApp().run(argc, argv);
}
