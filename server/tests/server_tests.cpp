#include <cassert>
#include <string>

#include "audio_studio/framework/service_registry.hpp"
#include "audio_studio/framework/status.hpp"

int main() {
  audio_studio::framework::Status ok = audio_studio::framework::Status::success();
  assert(ok.ok());
  assert(ok.codeString() == "OK");
  assert(ok.toJson().find("\"ok\":true") != std::string::npos);

  audio_studio::framework::ServiceRegistry registry;
  assert(registry.registerService("health", "server health service").ok());
  assert(registry.hasService("health"));
  assert(registry.describe("health") == "server health service");
  assert(!registry.registerService("", "bad").ok());
  assert(!registry.registerService("health", "duplicate").ok());

  return 0;
}
