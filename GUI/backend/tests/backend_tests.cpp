#include "audio_studio.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::string readText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void writeText(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out << text;
}

std::string testJsonStringField(const std::string& body, const std::string& field) {
  const std::string key = std::string("\"") + field + "\"";
  size_t pos = body.find(key);
  if (pos == std::string::npos) return {};
  pos = body.find(':', pos + key.size());
  if (pos == std::string::npos) return {};
  pos = body.find('"', pos + 1);
  if (pos == std::string::npos) return {};
  std::string out;
  bool esc = false;
  for (size_t i = pos + 1; i < body.size(); ++i) {
    const char c = body[i];
    if (esc) {
      out.push_back(c);
      esc = false;
      continue;
    }
    if (c == '\\') {
      esc = true;
      continue;
    }
    if (c == '"') return out;
    out.push_back(c);
  }
  return {};
}

fs::path makeTempRoot() {
  const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
  fs::path root = fs::temp_directory_path() / ("audio_studio_backend_test_" + std::to_string(stamp));
  fs::create_directories(root / "configs/platform/a2");
  fs::copy_file(fs::path(AUDIO_STUDIO_TEST_ROOT) / "configs/platform/a2/A2.json",
                root / "configs/platform/a2/A2.json",
                fs::copy_options::overwrite_existing);
  return root;
}

struct FakeCompileClient final : public audiostudio::IConfigCompileClient {
  bool ok = true;
  bool called = false;
  audiostudio::GuiConfigCompileRequest last_request;

  audiostudio::GuiConfigCompileResult compile(const audiostudio::GuiConfigCompileRequest& request) override {
    called = true;
    last_request = request;
    if (!ok) {
      audiostudio::GuiConfigCompileResult result;
      result.ok = false;
      result.message = "fake compile rejected file_io input";
      return result;
    }
    fs::create_directories(request.output_dir);
    const auto tplg = fs::path(request.output_dir) / "audio_studio.tplg";
    writeText(tplg, "fake tplg");
    audiostudio::GuiConfigCompileResult result;
    result.ok = true;
    result.tplg_path = tplg.string();
    return result;
  }
};

struct FakeValidationRunner final : public audiostudio::IValidationRunner {
  bool called = false;
  std::string last_test_list;
  std::string last_script;

  audiostudio::ValidationResult run(const audiostudio::ValidationRequest& request) override {
    called = true;
    last_test_list = request.test_list_path;
    last_script = request.script_path;
    return {true, "fake validation passed"};
  }
};

std::shared_ptr<audiostudio::BuildOrchestrator> makeOrchestrator(
    const fs::path& root,
    const std::shared_ptr<FakeCompileClient>& compile,
    const std::shared_ptr<FakeValidationRunner>& validation) {
  return std::make_shared<audiostudio::BuildOrchestrator>(root.string(), compile, validation);
}

} // namespace

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

  audiostudio::HttpServer server(AUDIO_STUDIO_TEST_ROOT, 0, engine, engine, engine, target_config, inspector);
  audiostudio::HttpRequest req;
  req.method = "POST";
  req.path = "/api/project/save";
  req.body = "{\"nodes\":[]}";
  auto res = server.handle(req);
  assert(res.status == 409);
  assert(res.body.find("\"ok\":false") != std::string::npos);
  assert(res.body.find("build_required") != std::string::npos);

  audiostudio::HttpRequest rpc;
  rpc.method = "POST";
  rpc.path = "/rpc";
  rpc.body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"server.health\"}";
  auto rpc_res = server.handle(rpc);
  assert(rpc_res.status == 200);
  assert(rpc_res.body.find("\"jsonrpc\":\"2.0\"") != std::string::npos);
  assert(rpc_res.body.find("\"result\"") != std::string::npos);

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

  audiostudio::HttpRequest projects;
  projects.method = "GET";
  projects.path = "/api/projects";
  auto projects_res = server.handle(projects);
  assert(projects_res.status == 200);
  assert(projects_res.body.find("a2/A2.json") != std::string::npos);
  assert(projects_res.body.find("simulator/simulator.json") != std::string::npos);

  audiostudio::HttpRequest project_config;
  project_config.method = "GET";
  project_config.path = "/api/config";
  project_config.query = "project=a2%2FA2.json";
  auto project_config_res = server.handle(project_config);
  assert(project_config_res.status == 200);
  assert(project_config_res.body.find("com.vsi.a2.audio_config") != std::string::npos);

  const fs::path temp_root = makeTempRoot();
  const fs::path temp_source = temp_root / "configs/platform/a2/A2.json";
  const std::string source_before_open = readText(temp_source);

  auto compile = std::make_shared<FakeCompileClient>();
  auto validation = std::make_shared<FakeValidationRunner>();
  auto orchestrator = makeOrchestrator(temp_root, compile, validation);
  audiostudio::HttpServer orchestrated_server(temp_root.string(), 0, engine, engine, engine,
                                              target_config, inspector, nullptr, nullptr, nullptr,
                                              nullptr, nullptr, nullptr, orchestrator);

  audiostudio::HttpRequest open_config;
  open_config.method = "GET";
  open_config.path = "/api/config";
  open_config.query = "project=a2%2FA2.json";
  auto open_config_res = orchestrated_server.handle(open_config);
  assert(open_config_res.status == 200);
  assert(open_config_res.body.find("com.vsi.a2.audio_config") != std::string::npos);
  assert(open_config_res.body.find("workspace_id") != std::string::npos);
  const std::string workspace_id = testJsonStringField(open_config_res.body, "workspace_id");
  assert(!workspace_id.empty());
  assert(readText(temp_source) == source_before_open);

  audiostudio::HttpRequest save_before_build;
  save_before_build.method = "POST";
  save_before_build.path = "/api/project/save";
  save_before_build.body = "{\"workspace_id\":\"" + workspace_id + "\"}";
  auto save_before_build_res = orchestrated_server.handle(save_before_build);
  assert(save_before_build_res.status == 409);
  assert(save_before_build_res.body.find("\"ok\":false") != std::string::npos);
  assert(save_before_build_res.body.find("build_required") != std::string::npos);

  audiostudio::HttpRequest build_req;
  build_req.method = "POST";
  build_req.path = "/api/pipeline/build";
  build_req.body = "{\"workspace_id\":\"" + workspace_id + "\",\"project\":\"a2/A2.json\","
                   "\"snapshot\":{\"pipeline\":{\"nodes\":[{\"id\":\"eq_1\",\"type\":\"builtin.eq\"}],"
                   "\"connections\":[]},\"params\":{\"eq_1\":{\"gain_db\":1}},"
                   "\"pipeline_descriptions\":[{\"pipe_id\":\"PLAYBACK_MAIN\",\"name\":\"Renamed Playback\"}],"
                   "\"file_io\":{\"input\":\"in.wav\",\"output\":\"out.wav\"}}}";
  auto build_res = orchestrated_server.handle(build_req);
  assert(build_res.status == 200);
  assert(build_res.body.find("\"ok\":true") != std::string::npos);
  assert(build_res.body.find("PIPE_LOADED") != std::string::npos);
  assert(build_res.body.find("RUNTIME") == std::string::npos);
  assert(compile->called);
  assert(validation->called);
  assert(compile->last_request.input_path.find(workspace_id) != std::string::npos);
  assert(compile->last_request.output_dir.find(workspace_id) != std::string::npos);
  assert(compile->last_request.project_name == "a2");
  assert(compile->last_request.build_tplg);
  assert(compile->last_request.strict);
  assert(compile->last_request.plugin_paths.empty());
  assert(compile->last_request.alsatplg.find("alsatplg") != std::string::npos);
  assert(validation->last_script.find("application/rv32qemu/sof-build-test.py") != std::string::npos);
  const std::string generated_test_list = readText(validation->last_test_list);
  assert(generated_test_list.find("ac_run --endpoint as_datalink --mtu 512") != std::string::npos);
  assert(generated_test_list.find("trace on") != std::string::npos);
  assert(generated_test_list.find("pipeinstall ") != std::string::npos);
  assert(generated_test_list.find("audio_studio.tplg") != std::string::npos);
  assert(generated_test_list.find("ac_run --stop") != std::string::npos);

  audiostudio::HttpRequest save_after_build;
  save_after_build.method = "POST";
  save_after_build.path = "/api/project/save";
  save_after_build.body = "{\"workspace_id\":\"" + workspace_id + "\"}";
  auto save_after_build_res = orchestrated_server.handle(save_after_build);
  assert(save_after_build_res.status == 200);
  assert(save_after_build_res.body.find("\"ok\":true") != std::string::npos);
  assert(readText(temp_source).find("\"audio_studio_gui\"") != std::string::npos);
  assert(readText(temp_source).find("\"file_io\"") != std::string::npos);
  assert(readText(temp_source).find("\"name\":\"Renamed Playback\"") != std::string::npos);

  const fs::path fail_root = makeTempRoot();
  auto fail_compile = std::make_shared<FakeCompileClient>();
  fail_compile->ok = false;
  auto fail_validation = std::make_shared<FakeValidationRunner>();
  auto fail_orchestrator = makeOrchestrator(fail_root, fail_compile, fail_validation);
  audiostudio::HttpServer fail_server(fail_root.string(), 0, engine, engine, engine,
                                      target_config, inspector, nullptr, nullptr, nullptr,
                                      nullptr, nullptr, nullptr, fail_orchestrator);
  audiostudio::HttpRequest fail_open;
  fail_open.method = "POST";
  fail_open.path = "/api/project/open";
  fail_open.body = "{\"project\":\"a2/A2.json\"}";
  auto fail_open_res = fail_server.handle(fail_open);
  const std::string fail_workspace_id = testJsonStringField(fail_open_res.body, "workspace_id");
  assert(!fail_workspace_id.empty());
  audiostudio::HttpRequest fail_build_req;
  fail_build_req.method = "POST";
  fail_build_req.path = "/api/pipeline/build";
  fail_build_req.body = "{\"workspace_id\":\"" + fail_workspace_id + "\",\"project\":\"a2/A2.json\","
                        "\"snapshot\":{\"pipeline\":{\"nodes\":[{\"id\":\"bad_node\",\"ports\":[{\"name\":\"in\"}]}]},"
                        "\"file_io\":{\"input\":\"missing.wav\"}}}";
  auto fail_build_res = fail_server.handle(fail_build_req);
  assert(fail_build_res.status == 200);
  assert(fail_build_res.body.find("\"ok\":false") != std::string::npos);
  assert(fail_build_res.body.find("\"status\":\"failed\"") != std::string::npos);
  assert(fail_build_res.body.find("\"stage\":\"compile\"") != std::string::npos);
  assert(fail_build_res.body.find("\"diagnostics\"") != std::string::npos);
  assert(fail_build_res.body.find("\"node_marks\"") != std::string::npos);
  assert(fail_build_res.body.find("\"port_marks\"") != std::string::npos);
  assert(fail_build_res.body.find("bad_node") != std::string::npos);
  assert(fail_build_res.body.find("in") != std::string::npos);
  assert(!fail_validation->called);

  audiostudio::HttpRequest builtin_catalog;
  builtin_catalog.method = "GET";
  builtin_catalog.path = "/configs/built-in-algorithm.json";
  auto builtin_catalog_res = server.handle(builtin_catalog);
  assert(builtin_catalog_res.status == 200);
  assert(builtin_catalog_res.body.find("builtin.file_input") != std::string::npos);

  engine->stop("{\"session_id\":\"standalone_html_demo\"}");
  assert(!engine->running());

  std::cout << "backend_tests passed\n";
  return 0;
}
