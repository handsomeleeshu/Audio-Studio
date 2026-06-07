#include "audio_studio.hpp"
#include <cassert>
#include <iostream>

int main() {
  audiostudio::MockRuntimeEngine engine;
  auto valid = engine.validatePipeline("{\"nodes\":[],\"edges\":[]}");
  assert(valid.find("\"ok\":true") != std::string::npos);
  auto invalid = engine.validatePipeline("{}");
  assert(invalid.find("\"ok\":false") != std::string::npos);
  auto build = engine.buildPipeline("{\"nodes\":[]}");
  assert(build.find("session_id") != std::string::npos);

  auto edit = engine.pipelineEditEvent("{\"action\":\"undo\",\"detail\":{\"label\":\"Move node\"}}");
  assert(edit.find("pipelineEditEvent") != std::string::npos);
  auto tool = engine.pipelineToolAction("{\"tool\":\"undo\",\"event\":\"tool_undo\"}");
  assert(tool.find("pipelineToolAction") != std::string::npos);

  engine.run("sess_test");
  assert(engine.running());
  auto tel = engine.telemetry({"IN", "EQ", "OUT"});
  assert(tel.find("nodeCost") != std::string::npos);
  assert(tel.find("cores") != std::string::npos);
  engine.stop("sess_test");
  assert(!engine.running());
  std::cout << "backend_tests passed\n";
  return 0;
}
