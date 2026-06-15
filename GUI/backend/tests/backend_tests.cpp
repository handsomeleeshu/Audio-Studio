#include "audio_studio.hpp"
#include <cassert>
#include <iostream>
#include <memory>

int main() {
  auto engine = std::make_shared<audiostudio::MockRuntimeEngine>();
  auto inspector = std::make_shared<audiostudio::FakeInspectorController>();
  auto target_config = std::make_shared<audiostudio::FakeTargetConfigController>();

  auto valid = engine->validatePipeline("{\"nodes\":[],\"connections\":[]}");
  assert(valid.find("\"ok\":true") != std::string::npos);

  auto build = engine->buildPipeline("{\"nodes\":[{\"id\":\"eq_1\"}]}");
  assert(build.find("session_id") != std::string::npos);

  auto edit = engine->pipelineEditEvent("{\"action\":\"node_moved\",\"detail\":{\"node_id\":\"eq_1\"}}");
  assert(edit.find("pipelineEditEvent") != std::string::npos);

  auto param = engine->updateParameter("{\"node_id\":\"agc_1\",\"param_id\":\"targetLevel\",\"value\":-16}");
  assert(param.find("IParameterController") != std::string::npos);

  engine->run("{\"session_id\":\"standalone_html_demo\"}");
  assert(engine->running());

  auto tel = engine->telemetry({"file_1","eq_1","out_1"});
  assert(tel.find("nodeCost") != std::string::npos);
  assert(tel.find("meters") != std::string::npos);

  auto inspect = inspector->inspectNode("{\"node_id\":\"eq_1\",\"node_name\":\"EQ\",\"module_type\":\"builtin.eq\"}");
  assert(inspect.find("IInspectorController") != std::string::npos);
  auto live = inspector->liveData({{"node_id","eq_1"},{"running","1"},{"ports","in:in:2,out:out:2"}});
  assert(live.find("\"ports\"") != std::string::npos);
  assert(live.find("\"general\"") != std::string::npos);
  auto inspect_buffer = inspector->inspectBuffer("{\"edge_key\":\"eq_1.out->out_1.in\",\"from\":\"eq_1.out\",\"to\":\"out_1.in\"}");
  assert(inspect_buffer.find("inspectBuffer") != std::string::npos);
  auto buffer_live = inspector->bufferLiveData({{"edge_key","eq_1.out->out_1.in"},{"running","1"},{"channels","2"},{"sample_rate","48000"},{"bits","16"}});
  assert(buffer_live.find("\"buffer\"") != std::string::npos);
  assert(buffer_live.find("\"pcm16\"") != std::string::npos);

  audiostudio::HttpServer server(".", 0, engine, engine, engine, target_config, inspector);
  audiostudio::HttpRequest req;
  req.method = "POST";
  req.path = "/api/project/save";
  req.body = "{\"nodes\":[]}";
  auto res = server.handle(req);
  assert(res.status == 200);
  assert(res.body.find("pipelineEditEvent") != std::string::npos);

  audiostudio::HttpRequest page;
  page.method = "GET";
  page.path = "/GUI/frontend/index.html";
  auto page_res = server.handle(page);
  assert(page_res.status == 200 || page_res.status == 404);

  audiostudio::HttpRequest legacy_page;
  legacy_page.method = "GET";
  legacy_page.path = "/frontend/index.html";
  auto legacy_page_res = server.handle(legacy_page);
  assert(legacy_page_res.status == 200 || legacy_page_res.status == 404);

  engine->stop("{\"session_id\":\"standalone_html_demo\"}");
  assert(!engine->running());

  std::cout << "backend_tests passed\n";
  return 0;
}
