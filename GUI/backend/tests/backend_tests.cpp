#include "audio_studio.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <type_traits>

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

std::string testJsonArrayField(const std::string& body, const std::string& field) {
  const std::string key = std::string("\"") + field + "\"";
  size_t pos = body.find(key);
  if (pos == std::string::npos) return {};
  pos = body.find(':', pos + key.size());
  if (pos == std::string::npos) return {};
  pos = body.find('[', pos + 1);
  if (pos == std::string::npos) return {};
  size_t depth = 0;
  bool in_string = false;
  bool esc = false;
  for (size_t i = pos; i < body.size(); ++i) {
    const char c = body[i];
    if (in_string) {
      if (esc) {
        esc = false;
      } else if (c == '\\') {
        esc = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (c == '"') {
      in_string = true;
    } else if (c == '[') {
      ++depth;
    } else if (c == ']') {
      --depth;
      if (depth == 0) return body.substr(pos, i - pos + 1);
    }
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
  fs::create_directories(root / "out/linux/a2/as_config/Debug");
  fs::create_directories(root / "out/linux/simulator/rpc_socket/Debug");
  writeText(root / "out/linux/a2/as_config/Debug/as_server", "stale a2 as_server");
  writeText(root / "out/linux/simulator/rpc_socket/Debug/as_server", "current simulator as_server");
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
  int start_count = 0;
  int stop_count = 0;
  std::string last_stopped_workspace;
  std::string last_test_list;
  std::string last_script;
  audiostudio::ValidationRequest last_request;

  audiostudio::ValidationResult start(const audiostudio::ValidationRequest& request) override {
    called = true;
    ++start_count;
    last_request = request;
    last_test_list = request.test_list_path;
    last_script = request.script_path;
    return {true, "fake validation passed"};
  }

  audiostudio::ValidationResult stop(const std::string& workspace_id) override {
    ++stop_count;
    last_stopped_workspace = workspace_id;
    return {true, "fake validation stopped"};
  }
};

std::shared_ptr<audiostudio::BuildOrchestrator> makeOrchestrator(
    const fs::path& root,
    const std::shared_ptr<FakeCompileClient>& compile,
    const std::shared_ptr<FakeValidationRunner>& validation) {
  return std::make_shared<audiostudio::BuildOrchestrator>(root.string(), compile, validation);
}

std::string validGuiPipelineSnapshot() {
  return R"({
    "active_pipeline":"PLAYBACK_MAIN",
    "group_id":"PLAYBACK_MAIN",
    "runtime_state":"NOT_READY",
    "pipeline_descriptions":[{"pipe_id":"PLAYBACK_MAIN","name":"Renamed Playback"}],
    "working_groups":[{
      "id":"PLAYBACK_MAIN",
      "pipeline_id":"PLAYBACK_MAIN",
      "name":"Renamed Playback",
      "origin_pipeline_ids":["PLAYBACK_MAIN"],
      "nodes":["PLAYBACK_MAIN__FILE_IN","PLAYBACK_MAIN__HOST_IN","PLAYBACK_MAIN__NEW_VOLUME","PLAYBACK_MAIN__DAI_OUT"],
      "edges":[
        "PLAYBACK_MAIN__FILE_IN:out->PLAYBACK_MAIN__HOST_IN:in",
        "PLAYBACK_MAIN__HOST_IN:out->PLAYBACK_MAIN__NEW_VOLUME:in",
        "PLAYBACK_MAIN__NEW_VOLUME:out->PLAYBACK_MAIN__DAI_OUT:in"
      ]
    }],
    "nodes":[
      {"id":"PLAYBACK_MAIN__FILE_IN","name":"File Input","module_type":"builtin.file_input","debug_file_io":true,
       "pipelineId":"PLAYBACK_MAIN","pipelineNodeId":"FILE_IN","in_ports":[],"out_ports":["out"],
       "port_domains":{"out":"external"}},
      {"id":"PLAYBACK_MAIN__HOST_IN","name":"Playback HOST","module_type":"builtin.host",
       "pipelineId":"PLAYBACK_MAIN","pipelineNodeId":"HOST_IN",
       "params":{"stream_name":"as_config_playback","direction":"playback","channels_min":1,"channels_max":2,"sample_bits":[16],"sample_rates":[48000]},
       "in_ports":["in"],"out_ports":["out"],"port_domains":{"in":"external","out":"sof"}},
      {"id":"PLAYBACK_MAIN__NEW_VOLUME","name":"Vol New","module_type":"gain.volume",
       "pipelineId":"PLAYBACK_MAIN","pipelineNodeId":"NEW_VOLUME",
       "params":{},
       "in_ports":["in"],"out_ports":["out"],"port_domains":{"in":"sof","out":"sof"}},
      {"id":"PLAYBACK_MAIN__DAI_OUT","name":"DAI FILE_IO Playback","module_type":"builtin.dai",
       "pipelineId":"PLAYBACK_MAIN","pipelineNodeId":"DAI_OUT",
       "params":{"dai_type":"file_io_dai","dai_index":0,"link_name":"FILE_IO_PLAYBACK_DAI0","device_id":"FILEIO0","direction":"playback","sample_rate":48000,"channels":2,"sample_bits":16,"tdm_slots":2,"slot_width":16},
       "in_ports":["in"],"out_ports":[],"port_domains":{"in":"sof"}}
    ],
    "connections":[
      {"from":"PLAYBACK_MAIN__FILE_IN.out","to":"PLAYBACK_MAIN__HOST_IN.in","from_domain":"external","to_domain":"external"},
      {"from":"PLAYBACK_MAIN__HOST_IN.out","to":"PLAYBACK_MAIN__NEW_VOLUME.in","from_domain":"sof","to_domain":"sof"},
      {"from":"PLAYBACK_MAIN__NEW_VOLUME.out","to":"PLAYBACK_MAIN__DAI_OUT.in","from_domain":"sof","to_domain":"sof"}
    ],
    "debug_file_io":[{"node_id":"PLAYBACK_MAIN__FILE_IN","direction":"input","file":{"file_name":"in.wav"}}]
  })";
}

std::string guiSnapshotWithWorkspace(const std::string& snapshot,
                                     const std::string& workspace_id,
                                     const std::string& project) {
  const size_t begin = snapshot.find('{');
  assert(begin != std::string::npos);
  std::ostringstream os;
  os << snapshot.substr(0, begin + 1)
     << "\"workspace_id\":\"" << workspace_id << "\","
     << "\"project\":\"" << project << "\","
     << snapshot.substr(begin + 1);
  return os.str();
}

std::string validGuiPipelineSnapshotWithUnselectedGroup() {
  std::string snapshot = validGuiPipelineSnapshot();
  const std::string group_marker = "    }],\n    \"nodes\":[";
  const size_t group_pos = snapshot.find(group_marker);
  assert(group_pos != std::string::npos);
  snapshot.replace(group_pos, group_marker.size(),
      "    },{\"id\":\"UNSELECTED_GROUP\",\"pipeline_id\":\"GUI_PIPE_2\",\"name\":\"Unselected\","
      "\"origin_pipeline_ids\":[],\"nodes\":[\"UNSELECTED__NODE\"],\"edges\":[]}],\n"
      "    \"nodes\":[");
  const std::string node_marker = "    ],\n    \"connections\":[";
  const size_t node_pos = snapshot.find(node_marker);
  assert(node_pos != std::string::npos);
  snapshot.replace(node_pos, node_marker.size(),
      "      ,{\"id\":\"UNSELECTED__NODE\",\"name\":\"Unselected\",\"module_type\":\"gain.volume\","
      "\"pipelineId\":\"GUI_PIPE_2\",\"pipelineNodeId\":\"NODE\",\"in_ports\":[\"in\"],"
      "\"out_ports\":[\"out\"],\"params\":{},\"port_domains\":{\"in\":\"sof\",\"out\":\"sof\"}}\n"
      "    ],\n"
      "    \"connections\":[");
  return snapshot;
}

std::string validCaptureGuiPipelineSnapshot() {
  return R"({
    "active_pipeline":"CAPTURE_MAIN",
    "group_id":"CAPTURE_MAIN",
    "runtime_state":"NOT_READY",
    "pipeline_descriptions":[{"pipe_id":"CAPTURE_MAIN","name":"AS Config Capture"}],
    "working_groups":[{
      "id":"CAPTURE_MAIN",
      "pipeline_id":"CAPTURE_MAIN",
      "name":"AS Config Capture",
      "origin_pipeline_ids":["CAPTURE_MAIN"],
      "nodes":["CAPTURE_MAIN__DAI_IN","CAPTURE_MAIN__HOST_OUT","CAPTURE_MAIN__FILE_OUT"],
      "edges":[
        "CAPTURE_MAIN__DAI_IN:out->CAPTURE_MAIN__HOST_OUT:in",
        "CAPTURE_MAIN__HOST_OUT:out->CAPTURE_MAIN__FILE_OUT:L"
      ]
    }],
    "nodes":[
      {"id":"CAPTURE_MAIN__DAI_IN","name":"DAI FILE_IO Capture","module_type":"builtin.dai",
       "pipelineId":"CAPTURE_MAIN","pipelineNodeId":"DAI_IN",
       "params":{"dai_type":"file_io_dai","dai_index":0,"link_name":"FILE_IO_CAPTURE_DAI0","device_id":"FILEIO0","direction":"capture","sample_rate":48000,"channels":2,"sample_bits":16,"tdm_slots":2,"slot_width":16},
       "in_ports":[],"out_ports":["out"],"port_domains":{"out":"sof"}},
      {"id":"CAPTURE_MAIN__HOST_OUT","name":"HOST Capture","module_type":"builtin.host",
       "pipelineId":"CAPTURE_MAIN","pipelineNodeId":"HOST_OUT",
       "params":{"stream_name":"as_config_capture","direction":"capture","channels_min":1,"channels_max":2,"sample_bits":[16],"sample_rates":[48000]},
       "in_ports":["in"],"out_ports":["out"],"port_domains":{"in":"sof","out":"external"}},
      {"id":"CAPTURE_MAIN__FILE_OUT","name":"File Output","module_type":"builtin.file_output","debug_file_io":true,
       "pipelineId":"CAPTURE_MAIN","pipelineNodeId":"FILE_OUT",
       "params":{"enable":true,"file_path":""},
       "in_ports":["L"],"out_ports":[],"port_domains":{"L":"external"}}
    ],
    "connections":[
      {"from":"CAPTURE_MAIN__DAI_IN.out","to":"CAPTURE_MAIN__HOST_OUT.in","from_domain":"sof","to_domain":"sof"},
      {"from":"CAPTURE_MAIN__HOST_OUT.out","to":"CAPTURE_MAIN__FILE_OUT.L","from_domain":"external","to_domain":"external"}
    ],
    "debug_file_io":[{"node_id":"CAPTURE_MAIN__FILE_OUT","direction":"output","file":{"file_name":"capture.wav"}}]
  })";
}

} // namespace

int main() {
  static_assert(!std::is_base_of_v<audiostudio::IRuntimeEngine, audiostudio::MockRuntimeEngine>,
                "MockRuntimeEngine must not be the production runtime implementation");
  static_assert(std::is_base_of_v<audiostudio::IRuntimeEngine, audiostudio::GuiRuntimeEngine>,
                "GuiRuntimeEngine must implement the production runtime interface");

  auto smoke_compile = std::make_shared<FakeCompileClient>();
  auto smoke_validation = std::make_shared<FakeValidationRunner>();
  auto smoke_orchestrator = makeOrchestrator(fs::path(AUDIO_STUDIO_TEST_ROOT), smoke_compile, smoke_validation);
  auto runtime = std::make_shared<audiostudio::GuiRuntimeEngine>(smoke_orchestrator);
  auto engine = std::make_shared<audiostudio::MockRuntimeEngine>();
  auto inspector = std::make_shared<audiostudio::FakeInspectorController>();
  auto target_config = std::make_shared<audiostudio::FakeTargetConfigController>();

  auto valid = runtime->validatePipeline("{\"nodes\":[],\"connections\":[]}");
  assert(valid.find("\"ok\":true") != std::string::npos);

  auto edit = runtime->pipelineEditEvent("{\"action\":\"node_moved\",\"detail\":{\"node_id\":\"eq_1\"}}");
  assert(edit.find("pipelineEditEvent") != std::string::npos);

  auto param = engine->updateParameter("{\"node_id\":\"agc_1\",\"param_id\":\"targetLevel\",\"value\":-16}");
  assert(param.find("IParameterController") != std::string::npos);

  auto run_result = runtime->run("{\"session_id\":\"standalone_html_demo\"}");
  if (run_result.find("\"ok\":false") == std::string::npos) {
    assert(runtime->telemetry({"file_1"}).find("\"running\":true") != std::string::npos);
  } else {
    assert(runtime->telemetry({"file_1"}).find("\"running\":false") != std::string::npos);
  }
  auto stream_accept = runtime->pushAudioStream("edge_key=file_1.out%2D%3Ehost_1.in", std::string(512, '\0'));
  assert(stream_accept.find("\"stream\":true") != std::string::npos);
  auto frame_accept = runtime->pushAudioFrame(
      "edge_key=file_1.out%2D%3Ehost_1.in",
      "{\"edge_key\":\"file_1.out->host_1.in\",\"frame_index\":0,\"frame_bytes\":512,\"bytes_written\":512,\"stream_ok\":true}");
  assert(frame_accept.find("\"frame\":true") != std::string::npos);

  auto tel = runtime->telemetry({"file_1","eq_1","out_1"});
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

  audiostudio::RpcAlgorithmCostController rpc_cost("127.0.0.1", 9);
  auto rpc_cost_disconnected = rpc_cost.liveCosts({{"nodes","eq_1"}});
  assert(rpc_cost_disconnected.find("\"connected\":false") != std::string::npos);
  assert(rpc_cost_disconnected.find("\"costs\":[]") != std::string::npos);
  audiostudio::RpcDspCoreLoadingController rpc_loading("127.0.0.1", 9);
  auto rpc_core_disconnected = rpc_loading.liveCoreLoading({{"cores","2"}});
  assert(rpc_core_disconnected.find("\"connected\":false") != std::string::npos);
  assert(rpc_core_disconnected.find("\"cores\":[]") != std::string::npos);
  audiostudio::RpcSystemHealthController rpc_health("127.0.0.1", 9);
  auto rpc_health_disconnected = rpc_health.liveHealth({});
  assert(rpc_health_disconnected.find("\"connected\":false") != std::string::npos);
  assert(rpc_health_disconnected.find("Audio Studio Info Heartbeat") != std::string::npos);

  audiostudio::HttpServer server(AUDIO_STUDIO_TEST_ROOT, 0, runtime, engine, engine, target_config, inspector);
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

  audiostudio::HttpRequest stream_req;
  stream_req.method = "POST";
  stream_req.path = "/api/runtime/audio/playback/stream";
  stream_req.query = "edge_key=eq_1.out%2D%3Eout_1.in";
  stream_req.body.assign(256, '\1');
  auto stream_res = server.handle(stream_req);
  assert(stream_res.body.find("\"stream\":true") != std::string::npos);
  assert(stream_res.body.find("\"queued_audio_ms\"") != std::string::npos);
  assert(stream_res.body.find("\"next_push_ms\"") != std::string::npos);

  audiostudio::HttpRequest frame_req;
  frame_req.method = "POST";
  frame_req.path = "/api/runtime/audio/playback/frame";
  frame_req.query = "edge_key=eq_1.out%2D%3Eout_1.in";
  frame_req.body = "{\"edge_key\":\"eq_1.out->out_1.in\",\"frame_index\":1,\"frame_bytes\":256,\"bytes_written\":256,\"stream_ok\":true}";
  auto frame_res = server.handle(frame_req);
  assert(frame_res.body.find("\"frame\":true") != std::string::npos);
  assert(frame_res.body.find("\"queued_audio_ms\"") != std::string::npos);
  assert(frame_res.body.find("\"next_push_ms\"") != std::string::npos);

  audiostudio::HttpRequest eos_req;
  eos_req.method = "POST";
  eos_req.path = "/api/runtime/audio/playback/eos";
  eos_req.body = "{\"session_id\":\"standalone_html_demo\",\"edge_key\":\"eq_1.out->out_1.in\"}";
  auto eos_res = server.handle(eos_req);
  assert(eos_res.status == 200);
  assert(eos_res.body.find("\"eos\":true") != std::string::npos);
  assert(eos_res.body.find("\"runtime_state\":\"PIPE_LOADED\"") != std::string::npos);

  auto capture_start = runtime->run("{\"session_id\":\"capture_demo\",\"capture\":{\"enabled\":true,\"sample_rate\":48000,\"channels\":2,\"bits_per_sample\":16,\"frame_bytes\":512}}");
  assert(capture_start.find("\"capture\"") != std::string::npos);
  auto combined_start = runtime->run(
      "{\"session_id\":\"combined_demo\","
      "\"playback\":{\"enabled\":true,\"node_id\":\"play_file\",\"device\":\"as_config_playback\",\"sample_rate\":48000,\"channels\":1,\"bits_per_sample\":16,\"frame_bytes\":512},"
      "\"capture\":{\"enabled\":true,\"node_id\":\"cap_file\",\"file_name\":\"cap.wav\",\"device\":\"as_config_capture\",\"sample_rate\":48000,\"channels\":2,\"bits_per_sample\":16,\"frame_bytes\":512}}");
  assert(combined_start.find("\"capture\"") != std::string::npos);
  assert(combined_start.find("\"node_id\":\"cap_file\"") != std::string::npos);
  assert(combined_start.find("\"node_id\":\"play_file\"") == std::string::npos ||
         combined_start.find("\"node_id\":\"cap_file\"") < combined_start.find("\"node_id\":\"play_file\""));
  audiostudio::HttpRequest capture_frame_req;
  capture_frame_req.method = "GET";
  capture_frame_req.path = "/api/runtime/audio/capture/frame";
  capture_frame_req.query = "max_bytes=512";
  auto capture_frame_res = server.handle(capture_frame_req);
  assert(capture_frame_res.status == 200);
  assert(capture_frame_res.body.find("\"capture\"") != std::string::npos);
  assert(capture_frame_res.body.find("\"queued_bytes\":0") != std::string::npos);
  assert(capture_frame_res.body.find("\"next_poll_ms\"") != std::string::npos);
  assert(capture_frame_res.body.find("\"data_base64\"") != std::string::npos ||
         capture_frame_res.body.find("\"ok\":false") != std::string::npos);

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
  auto orchestrated_runtime = std::make_shared<audiostudio::GuiRuntimeEngine>(orchestrator);
  audiostudio::HttpServer orchestrated_server(temp_root.string(), 0, orchestrated_runtime, engine, engine,
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
  const std::string workspace_all_path = testJsonStringField(open_config_res.body, "workspace_path");
  assert(!workspace_id.empty());
  assert(!workspace_all_path.empty());
  assert(workspace_all_path.find("a2_pipeline_all.json") != std::string::npos);
  const std::string opened_workspace_json = readText(workspace_all_path);
  const std::string opened_pipelines = testJsonArrayField(opened_workspace_json, "pipelines");
  assert(opened_pipelines.find("PLAYBACK_MAIN") != std::string::npos);
  assert(opened_pipelines.find("CAPTURE_MAIN") != std::string::npos);
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
                   "\"snapshot\":" + validGuiPipelineSnapshotWithUnselectedGroup() + "}";
  auto build_res = orchestrated_server.handle(build_req);
  assert(build_res.status == 200);
  assert(build_res.body.find("\"ok\":true") != std::string::npos);
  assert(build_res.body.find("PIPE_LOADED") != std::string::npos);
  assert(build_res.body.find("RUNTIME") == std::string::npos);
  assert(build_res.body.find("\"updated_pipelines\"") != std::string::npos);
  assert(build_res.body.find("\"updated_module_instances\"") == std::string::npos);
  assert(build_res.body.find("\"updated_frontend_connections\"") != std::string::npos);
  assert(build_res.body.find("\"workspace_revision\"") != std::string::npos);
  assert(compile->called);
  assert(validation->called);
  assert(compile->last_request.input_path.find(workspace_id) != std::string::npos);
  assert(compile->last_request.input_path.find("a2_pipeline_all.json") != std::string::npos);
  assert(compile->last_request.output_dir.find(workspace_id) != std::string::npos);
  assert(compile->last_request.project_name == "a2");
  assert(compile->last_request.working_dir == temp_root.string());
  assert(fs::path(compile->last_request.working_dir).is_absolute());
  assert(compile->last_request.build_tplg);
  assert(compile->last_request.strict);
  assert(compile->last_request.plugin_paths.empty());
  assert(compile->last_request.alsatplg.find("alsatplg") != std::string::npos);
  assert(fs::path(compile->last_request.alsatplg).is_absolute());
  assert(compile->last_request.as_server.find("as_server") != std::string::npos);
  assert(compile->last_request.as_server.find("out/linux/simulator/rpc_socket/Debug/as_server") != std::string::npos);
  assert(validation->last_script.find("application/rv32qemu/sof-build-test.py") != std::string::npos);
  assert(validation->last_request.as_server_path.find("as_server") != std::string::npos);
  assert(validation->last_request.as_log_path.find("as_log") != std::string::npos);
  assert(validation->last_request.trace_ldc_path.find("application/rv32qemu/build/sof.ldc") != std::string::npos);
  const std::string generated_test_list = readText(validation->last_test_list);
  assert(generated_test_list.find("ac_run --endpoint as_datalink --mtu 512") != std::string::npos);
  assert(generated_test_list.find("trace on") != std::string::npos);
  assert(generated_test_list.find("pipeinstall -p 1 ") != std::string::npos);
  assert(generated_test_list.find("sleep 3600") != std::string::npos);
  assert(generated_test_list.find("audio_studio.tplg") != std::string::npos);
  assert(generated_test_list.find("ac_run --stop") == std::string::npos);

  const std::string global_pipeline_json_after_build = readText(compile->last_request.input_path);
  const std::string regenerated_pipelines = testJsonArrayField(global_pipeline_json_after_build, "pipelines");
  assert(!regenerated_pipelines.empty());
  assert(regenerated_pipelines.find("\"pipe_id\":\"PLAYBACK_MAIN\"") != std::string::npos);
  assert(regenerated_pipelines.find("\"pipe_id\":\"GUI_PIPE_2\"") != std::string::npos);
  assert(regenerated_pipelines.find("\"pipe_id\":\"CAPTURE_MAIN\"") == std::string::npos);
  assert(regenerated_pipelines.find("\"pipe_id\":\"DSP_FILTER_COVERAGE\"") == std::string::npos);
  assert(regenerated_pipelines.find("builtin.file_input") == std::string::npos);
  assert(regenerated_pipelines.find("builtin.file_output") == std::string::npos);
  assert(regenerated_pipelines.find("FILE_IN") == std::string::npos);
  assert(regenerated_pipelines.find("\"node_id\":\"NEW_VOLUME\"") != std::string::npos);
  assert(regenerated_pipelines.find("\"kind\"") == std::string::npos);
  assert(regenerated_pipelines.find("\"inst_ref\"") == std::string::npos);
  assert(regenerated_pipelines.find("\"module_type\":\"builtin.host\"") != std::string::npos);
  assert(regenerated_pipelines.find("\"module_type\":\"builtin.dai\"") != std::string::npos);
  assert(regenerated_pipelines.find("\"module_type\":\"gain.volume\"") != std::string::npos);
  assert(regenerated_pipelines.find("FILE_IN:out") == std::string::npos);
  assert(regenerated_pipelines.find("\"from\":\"HOST_IN:out\"") != std::string::npos);
  assert(regenerated_pipelines.find("\"to\":\"NEW_VOLUME:in\"") != std::string::npos);
  assert(global_pipeline_json_after_build.find("\"module_instances\"") == std::string::npos);
  const std::string regenerated_frontend_connections = testJsonArrayField(global_pipeline_json_after_build, "frontend_connections");
  assert(regenerated_frontend_connections.find("\"module_type\":\"builtin.file_input\"") != std::string::npos);
  assert(regenerated_frontend_connections.find("\"from\":\"FILE_IN:out\"") != std::string::npos);
  assert(regenerated_frontend_connections.find("\"to\":\"HOST_IN:in\"") != std::string::npos);
  assert(global_pipeline_json_after_build.find("\"node_id\":\"VOLUME\"") == std::string::npos);

  const std::string all_workspace_json_after_build = readText(workspace_all_path);
  const std::string all_pipelines_after_build = testJsonArrayField(all_workspace_json_after_build, "pipelines");
  assert(all_pipelines_after_build.find("PLAYBACK_MAIN") != std::string::npos);
  assert(all_pipelines_after_build.find("GUI_PIPE_2") != std::string::npos);
  assert(all_pipelines_after_build.find("CAPTURE_MAIN") == std::string::npos);
  assert(all_pipelines_after_build.find("DSP_FILTER_COVERAGE") == std::string::npos);
  assert(all_pipelines_after_build.find("\"name\":\"Renamed Playback\"") != std::string::npos);
  assert(all_pipelines_after_build.find("builtin.file_input") == std::string::npos);

  compile->called = false;
  validation->called = false;
  audiostudio::HttpRequest raw_build_req;
  raw_build_req.method = "POST";
  raw_build_req.path = "/api/pipeline/build";
  raw_build_req.body = guiSnapshotWithWorkspace(validGuiPipelineSnapshot(), workspace_id, "a2/A2.json");
  const int start_count_before_rebuild = validation->start_count;
  const int stop_count_before_rebuild = validation->stop_count;
  auto raw_build_res = orchestrated_server.handle(raw_build_req);
  assert(raw_build_res.status == 200);
  assert(raw_build_res.body.find("\"ok\":true") != std::string::npos);
  assert(raw_build_res.body.find("PIPE_LOADED") != std::string::npos);
  assert(compile->called);
  assert(validation->start_count == start_count_before_rebuild + 1);
  assert(validation->stop_count >= stop_count_before_rebuild + 1);
  assert(validation->last_stopped_workspace == workspace_id);

  audiostudio::HttpRequest unload_req;
  unload_req.method = "POST";
  unload_req.path = "/api/pipeline/unload";
  unload_req.body = raw_build_req.body;
  auto unload_res = orchestrated_server.handle(unload_req);
  assert(unload_res.status == 200);
  assert(unload_res.body.find("\"ok\":true") != std::string::npos);
  assert(unload_res.body.find("\"runtime_state\":\"NOT_READY\"") != std::string::npos);

  raw_build_res = orchestrated_server.handle(raw_build_req);
  assert(raw_build_res.status == 200);
  assert(raw_build_res.body.find("\"ok\":true") != std::string::npos);
  assert(raw_build_res.body.find("PIPE_LOADED") != std::string::npos);
  assert(compile->called);
  assert(validation->called);

  audiostudio::HttpRequest scoped_build_req;
  scoped_build_req.method = "POST";
  scoped_build_req.path = "/api/pipeline/build";
  scoped_build_req.body = "{\"workspace_id\":\"" + workspace_id + "\",\"project\":\"a2/A2.json\","
                          "\"snapshot\":" + validGuiPipelineSnapshotWithUnselectedGroup() + "}";
  auto scoped_build_res = orchestrated_server.handle(scoped_build_req);
  assert(scoped_build_res.status == 200);
  assert(scoped_build_res.body.find("\"ok\":true") != std::string::npos);
  const std::string scoped_workspace_json = readText(compile->last_request.input_path);
  const std::string scoped_pipelines = testJsonArrayField(scoped_workspace_json, "pipelines");
  assert(scoped_pipelines.find("\"pipe_id\":\"PLAYBACK_MAIN\"") != std::string::npos);
  assert(scoped_pipelines.find("GUI_PIPE_2") != std::string::npos);

  auto relative_root_abs = makeTempRoot();
  auto relative_compile = std::make_shared<FakeCompileClient>();
  auto relative_validation = std::make_shared<FakeValidationRunner>();
  const auto relative_root = fs::relative(relative_root_abs, fs::current_path());
  auto relative_orchestrator = makeOrchestrator(relative_root, relative_compile, relative_validation);
  auto relative_open = relative_orchestrator->openProjectByName("a2/A2.json");
  assert(relative_open.status == 200);
  const std::string relative_workspace_id = testJsonStringField(relative_open.body, "workspace_id");
  assert(!relative_workspace_id.empty());
  auto relative_build = relative_orchestrator->buildPipeline(
      "{\"workspace_id\":\"" + relative_workspace_id + "\",\"project\":\"a2/A2.json\","
      "\"snapshot\":" + validGuiPipelineSnapshot() + "}");
  assert(relative_build.status == 200);
  assert(relative_build.body.find("\"ok\":true") != std::string::npos);
  assert(fs::path(relative_compile->last_request.working_dir).is_absolute());
  assert(fs::path(relative_compile->last_request.alsatplg).is_absolute());
  assert(fs::path(relative_compile->last_request.as_server).is_absolute());
  fs::remove_all(relative_root_abs);

  audiostudio::HttpRequest capture_after_playback_build_req;
  capture_after_playback_build_req.method = "POST";
  capture_after_playback_build_req.path = "/api/pipeline/build";
  capture_after_playback_build_req.body = "{\"workspace_id\":\"" + workspace_id + "\",\"project\":\"a2/A2.json\","
                                          "\"snapshot\":" + validCaptureGuiPipelineSnapshot() + "}";
  auto capture_after_playback_build_res = orchestrated_server.handle(capture_after_playback_build_req);
  assert(capture_after_playback_build_res.status == 200);
  assert(capture_after_playback_build_res.body.find("\"ok\":true") != std::string::npos);
  const std::string capture_workspace_json = readText(compile->last_request.input_path);
  const std::string capture_pipelines = testJsonArrayField(capture_workspace_json, "pipelines");
  assert(capture_pipelines.find("\"pipe_id\":\"CAPTURE_MAIN\"") != std::string::npos);
  assert(capture_pipelines.find("\"inst_ref\"") == std::string::npos);
  assert(capture_pipelines.find("\"module_type\":\"builtin.dai\"") != std::string::npos);
  assert(capture_pipelines.find("virtual.audio_output") == std::string::npos);

  auto restore_scoped_build_res = orchestrated_server.handle(scoped_build_req);
  assert(restore_scoped_build_res.status == 200);
  assert(restore_scoped_build_res.body.find("\"ok\":true") != std::string::npos);

  audiostudio::HttpRequest save_after_build;
  save_after_build.method = "POST";
  save_after_build.path = "/api/project/save";
  save_after_build.body = "{\"workspace_id\":\"" + workspace_id + "\"}";
  auto save_after_build_res = orchestrated_server.handle(save_after_build);
  assert(save_after_build_res.status == 200);
  assert(save_after_build_res.body.find("\"ok\":true") != std::string::npos);
  assert(readText(temp_source).find("\"audio_studio_gui\"") != std::string::npos);
  assert(readText(temp_source).find("\"debug_file_io\"") != std::string::npos);
  assert(readText(temp_source).find("\"name\":\"Renamed Playback\"") != std::string::npos);

  const fs::path fail_root = makeTempRoot();
  auto fail_compile = std::make_shared<FakeCompileClient>();
  fail_compile->ok = false;
  auto fail_validation = std::make_shared<FakeValidationRunner>();
  auto fail_orchestrator = makeOrchestrator(fail_root, fail_compile, fail_validation);
  auto fail_runtime = std::make_shared<audiostudio::GuiRuntimeEngine>(fail_orchestrator);
  audiostudio::HttpServer fail_server(fail_root.string(), 0, fail_runtime, engine, engine,
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
                        "\"snapshot\":" + validGuiPipelineSnapshot() + "}";
  auto fail_build_res = fail_server.handle(fail_build_req);
  assert(fail_build_res.status == 200);
  assert(fail_build_res.body.find("\"ok\":false") != std::string::npos);
  assert(fail_build_res.body.find("\"status\":\"failed\"") != std::string::npos);
  assert(fail_build_res.body.find("\"stage\":\"compile\"") != std::string::npos);
  assert(fail_build_res.body.find("\"updated_pipelines\"") != std::string::npos);
  assert(fail_build_res.body.find("\"updated_module_instances\"") == std::string::npos);
  assert(fail_build_res.body.find("\"updated_frontend_connections\"") != std::string::npos);
  assert(fail_build_res.body.find("\"diagnostics\"") != std::string::npos);
  assert(fail_build_res.body.find("\"node_marks\"") != std::string::npos);
  assert(fail_build_res.body.find("\"port_marks\"") != std::string::npos);
  assert(!fail_validation->called);

  const fs::path invalid_root = makeTempRoot();
  auto invalid_compile = std::make_shared<FakeCompileClient>();
  auto invalid_validation = std::make_shared<FakeValidationRunner>();
  auto invalid_orchestrator = makeOrchestrator(invalid_root, invalid_compile, invalid_validation);
  auto invalid_runtime = std::make_shared<audiostudio::GuiRuntimeEngine>(invalid_orchestrator);
  audiostudio::HttpServer invalid_server(invalid_root.string(), 0, invalid_runtime, engine, engine,
                                         target_config, inspector, nullptr, nullptr, nullptr,
                                         nullptr, nullptr, nullptr, invalid_orchestrator);
  audiostudio::HttpRequest invalid_open;
  invalid_open.method = "POST";
  invalid_open.path = "/api/project/open";
  invalid_open.body = "{\"project\":\"a2/A2.json\"}";
  auto invalid_open_res = invalid_server.handle(invalid_open);
  const std::string invalid_workspace_id = testJsonStringField(invalid_open_res.body, "workspace_id");
  assert(!invalid_workspace_id.empty());
  audiostudio::HttpRequest invalid_build_req;
  invalid_build_req.method = "POST";
  invalid_build_req.path = "/api/pipeline/build";
  invalid_build_req.body = "{\"workspace_id\":\"" + invalid_workspace_id + "\",\"project\":\"a2/A2.json\","
                           "\"snapshot\":{\"working_groups\":[{\"id\":\"GUI_PIPE_1\",\"nodes\":[\"GUI_HOST\"],\"edges\":[]}],"
                           "\"nodes\":[{\"id\":\"GUI_HOST\",\"name\":\"HOST\","
                           "\"pipelineId\":\"GUI_PIPE_1\",\"pipelineNodeId\":\"HOST\",\"in_ports\":[\"in\"],\"out_ports\":[\"out\"],"
                           "\"port_domains\":{\"in\":\"external\",\"out\":\"sof\"}}],\"connections\":[]}}";
  auto invalid_build_res = invalid_server.handle(invalid_build_req);
  assert(invalid_build_res.status == 200);
  assert(invalid_build_res.body.find("\"ok\":false") != std::string::npos);
  assert(invalid_build_res.body.find("\"stage\":\"workspace\"") != std::string::npos);
  assert(invalid_build_res.body.find("pipeline node requires module_type") != std::string::npos);
  assert(!invalid_compile->called);
  assert(!invalid_validation->called);

  audiostudio::HttpRequest builtin_catalog;
  builtin_catalog.method = "GET";
  builtin_catalog.path = "/configs/built-in-algorithm.json";
  auto builtin_catalog_res = server.handle(builtin_catalog);
  assert(builtin_catalog_res.status == 200);
  assert(builtin_catalog_res.body.find("builtin.file_input") != std::string::npos);

  runtime->stop("{\"session_id\":\"standalone_html_demo\"}");
  assert(runtime->telemetry({"file_1"}).find("\"running\":false") != std::string::npos);

  std::cout << "backend_tests passed\n";
  return 0;
}
