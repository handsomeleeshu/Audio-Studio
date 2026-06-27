import { strict as assert } from 'assert';
import fs from 'fs';

const html = fs.readFileSync(new URL('../../GUI/frontend/index.html', import.meta.url), 'utf8');

function assertIncludes(token, message = `missing token: ${token}`) {
  assert.ok(html.includes(token), message);
}

function assertNotIncludes(token, message = `unexpected token: ${token}`) {
  assert.equal(html.includes(token), false, message);
}

function count(pattern) {
  return (html.match(pattern) || []).length;
}

assertIncludes('const RUNTIME_STATES = Object.freeze({', 'frontend should expose the canonical runtime enum');
for (const state of ['NOT_READY', 'PIPE_UNLOADED', 'PIPE_LOADED', 'PIPE_RUNNING', 'ERROR']) {
  assertIncludes(state, `runtime enum must include ${state}`);
}
assertNotIncludes('validated: { label:', 'validated must not remain an API/runtime state');
assertNotIncludes("built: { label:", 'built must be mapped to PIPE_LOADED instead of exposed as built');
assertNotIncludes('stopped: { label:', 'stopped must not remain an API/runtime state');
assert.equal(/['"]RUNTIME['"]/.test(html), false, 'runtime gating must not use the legacy RUNTIME state token');

assert.equal(count(/#buildBtn'\)\.addEventListener\('click'/g), 1, 'Build button should have one owning click handler');
assertIncludes('async function buildRuntimeGroup', 'Build button should wait for /api/pipeline/build before changing state');
assertIncludes("buildBtn.disabled = true", 'Build button should be disabled while building');
assertIncludes('res && res.ok === true', 'Build must not treat null/missing backend responses as success');
assertIncludes('normalizeRuntimeStatus(res?.runtime_state) === RUNTIME_STATES.PIPE_LOADED', 'Build success must require backend PIPE_LOADED');
assert.ok(/setRuntimeStatusForGroup\(groupId,[\s\S]*RUNTIME_STATES\.PIPE_LOADED/.test(html), 'successful build should set PIPE_LOADED');
assert.ok(/setRuntimeStatusForGroup\(groupId,[\s\S]*RUNTIME_STATES\.ERROR/.test(html), 'failed build should set ERROR');
assertIncludes('applyBuildDiagnostics', 'build diagnostics should mark nodes and ports');
assertIncludes('node_marks', 'backend node marks must be consumed');
assertIncludes('port_marks', 'backend port marks must be consumed');
assertNotIncludes('UI keeps running in fallback mode', 'API failures must not be described as fallback success mode');
assertNotIncludes("addLog('ok', 'Build successful', 'Smart Speaker v3 / HiFi5')", 'startup must not log fake build success');

assertIncludes('ensureInspectorPreset', 'Inspector should create or find inspector_preset');
assertIncludes("preset_id: 'inspector_preset'", 'inspector preset id should be stable');
assertIncludes('moduleParamDefaultValue', 'Inspector should fall back to module parameter defaults');
assertIncludes('function snapshotParamsForNode', 'pipeline snapshot must preserve node paramValues loaded from platform JSON');
assert.ok(
  /function snapshotParamsForNode[\s\S]*n\?\.paramValues[\s\S]*inspectorPresetValuesForNode/.test(html),
  'snapshot params should merge platform-loaded node paramValues with Inspector overrides'
);
assert.ok(
  /function componentFromResolvedModule[\s\S]*c\.paramValues\s*=\s*\{\s*\.\.\.params\s*\}/.test(html),
  'nodes created from platform JSON should keep their original params in paramValues'
);
assertIncludes('isParamEnabledForNode', 'Inspector controls should be gated by runtime state');
assertIncludes('writeInspectorPresetParam', 'Inspector changes should write to inspector_preset');
assertIncludes('data-param-disabled', 'Inspector disabled state should be reflected in DOM');

assertIncludes('debug_file_io', 'snapshot should carry debug file I/O outside as_config payload');
assertIncludes('frontend_connections', 'pipeline layout should restore frontend File I/O nodes from platform JSON');
assertIncludes('addFrontendConnectionsToPipelineGraph', 'pipeline layout load should merge frontend connections with SOF pipeline nodes');
assertIncludes('working_groups', 'snapshot should send working groups for backend pipeline regeneration');
assertIncludes('pipelineNodeId', 'snapshot should send original/local pipeline node ids');
assertNotIncludes('inst_ref', 'snapshot must not send removed module instance references');
assertIncludes('port_domains', 'snapshot should send per-port SOF/external domains');
assertIncludes('data-file-param-action', 'file_open/file_save controls should be rendered');
assertIncludes("t === 'file_io'", 'Inspector should map file_io value_type to file open/save controls');
assertIncludes('readAudioFileInfo', 'WAV selection should parse basic audio info');
assertIncludes('/api/runtime/audio/playback/stream', 'file input playback must send PCM bytes through the playback stream URL');
assertIncludes("apiPostBinary('/api/runtime/audio/playback/stream?'", 'binary playback payload must use the playback stream URL');
assertIncludes("apiPost('/api/runtime/audio/playback/frame?'", 'playback frame API should send metadata as JSON');
assertNotIncludes("apiPostBinary('/api/runtime/audio/playback/frame?'", 'playback frame API must not carry binary PCM payload');
assertNotIncludes('/api/runtime/audio/stream', 'frontend must not use unscoped audio stream routes');
assertNotIncludes('/api/runtime/audio/frame', 'frontend must not use unscoped audio frame routes');
assertIncludes('/api/runtime/audio/playback/eos', 'file input playback must send explicit EOS after the last WAV frame');
assertIncludes('finishPlaybackFramePump', 'playback pump should wait for backend EOS result before restoring RUN state');
assert.ok(
  /async function startRuntimeGroup[\s\S]*current === RUNTIME_STATES\.PIPE_RUNNING[\s\S]*return true[\s\S]*postRuntimeGroupState\('run'/.test(html),
  'PIPE_RUNNING groups should ignore duplicate RUN clicks before posting /api/runtime/run'
);
assertIncludes('startCaptureFramePump', 'file output capture should start a backend-driven frame pump');
assertIncludes('/api/runtime/audio/capture/frame', 'file output capture should pull frames from GUI backend');
assertIncludes('capture_request', 'runtime run response should carry capture request metadata');
assertIncludes('runtimeDeviceNameForEdge', 'file I/O runtime requests should pass the connected HOST stream name to GUI backend');
assert.ok(/setRuntimeStatusForGroup\(groupId,\s*RUNTIME_STATES\.PIPE_LOADED,\s*ok \? ['"]run_complete['"] : ['"]run_error['"]\)/.test(html), 'EOS completion should restore the selected group to PIPE_LOADED');
assert.ok(
  /function isParamEnabledForNode[\s\S]*return states\.includes\(status\)/.test(html),
  'parameter editability should be driven directly by the JSON settable state list'
);
assert.ok(
  !/status === RUNTIME_STATES\.PIPE_UNLOADED[\s\S]*states\.includes\(RUNTIME_STATES\.PIPE_LOADED\)/.test(html),
  'PIPE_LOADED must not implicitly enable PIPE_UNLOADED edits'
);
assertIncludes('validateRuntimeFileIoForGroup', 'RUN should validate frontend File I/O selections before contacting backend runtime');
assert.ok(
  /async function startRuntimeGroup[\s\S]*validateRuntimeFileIoForGroup\(groupId\)[\s\S]*postRuntimeGroupState\('run'/.test(html),
  'RUN must reject missing/invalid File I/O before POST /api/runtime/run'
);
assert.ok(
  /(?:const|let) ok = !!res && res\.ok === true && normalizeRuntimeStatus\(res\?\.runtime_state\) === RUNTIME_STATES\.PIPE_RUNNING/.test(html),
  'RUN must require an explicit successful PIPE_RUNNING response before starting frontend pumps'
);
assert.ok(
  /setRuntimeStatusForGroup\(groupId, ok \? RUNTIME_STATES\.PIPE_RUNNING : RUNTIME_STATES\.PIPE_LOADED/.test(html),
  'a rejected RUN request should restore the pre-run PIPE_LOADED state'
);
assert.ok(
  /finishPlaybackFramePump[\s\S]*setRuntimeStatusForGroup\(groupId, RUNTIME_STATES\.PIPE_LOADED, ok \? ['"]run_complete['"] : ['"]run_error['"]\)/.test(html),
  'playback EOS success or failure should leave the built pipeline loaded'
);
assert.ok(
  /pumpCaptureFrame[\s\S]*catch \(e\)[\s\S]*postRuntimeGroupState\('stop', groupId, RUNTIME_STATES\.PIPE_LOADED\)[\s\S]*setRuntimeStatusForGroup\(groupId, RUNTIME_STATES\.PIPE_LOADED, ['"]run_error['"]\)/.test(html),
  'capture frame failures should stop the backend session and restore PIPE_LOADED'
);
assertIncludes('assertValidWavInfo', 'WAV file selection must reject invalid local files before runtime requests');
assertIncludes('applyDaiInputAudioInfoToParams', 'File_IO DAI input WAV selection should update DAI audio format params for build');
assertIncludes('data-dai-file-param-action', 'File_IO DAI source nodes should expose a frontend WAV chooser');
assertIncludes("paramId(p) === 'file_path'", 'FILE_IO DAI file_path should suppress the legacy duplicate input control');
assertIncludes('choose a DAI output WAV path before RUN', 'FILE_IO DAI output should require an explicit frontend save path');
assertIncludes('/api/runtime/audio/dai/input', 'FILE_IO DAI input WAV bytes must use a dedicated backend stream URL');
assertIncludes('/api/runtime/audio/dai/output', 'FILE_IO DAI output WAV bytes must use a dedicated backend stream URL');
assertIncludes('stageDaiInputsForGroup', 'RUN must stage selected FILE_IO DAI input files before starting SOF');
assertIncludes('saveDaiOutputsForGroup', 'playback completion must save FILE_IO DAI output files to the frontend selection');
assertIncludes('expected_data_bytes', 'FILE_IO DAI capture should derive an automatic EOS byte count from the selected input WAV');
assertIncludes('finishCaptureFramePump', 'capture should stop the backend and finalize the WAV when the selected DAI input reaches EOS');
assert.ok(
  /pumpCaptureFrame[\s\S]*captureFramePump\.bytes >= captureFramePump\.expectedBytes[\s\S]*finishCaptureFramePump\('eof'\)/.test(html),
  'capture frame pumping should automatically finish at the selected FILE_IO DAI input duration'
);
assert.ok(
  /apiPost\('\/api\/param\/update'[\s\S]*workspace_id:[\s\S]*pipeline_node_id:[\s\S]*runtime_state:/.test(html),
  'runtime parameter updates should include workspace and original node identity for backend inspector_preset storage'
);
assertIncludes('isDebugFileIoNode', 'debug file I/O nodes should be identifiable');
assert.ok(/function isDebugFileIoNode[\s\S]*id === 'builtin\.file_output'/.test(html), 'builtin file output must be treated as a debug file I/O node');
assertIncludes('as_config_nodes', 'snapshot should separate final as_config payload nodes');
assertIncludes("id === 'builtin.host'", 'debug file I/O connection policy should recognize host modules');
assertIncludes('isExternalPort', 'frontend should distinguish external/debug ports');
assertIncludes('data-port-domain', 'port DOM should expose SOF/external domain');
assertIncludes('port-domain-external', 'external/debug ports should render as solid special ports');
assertIncludes('External debug ports can connect only to external debug ports', 'connection policy should reject external to SOF edges');

assertIncludes('isDaiFileIoNode', 'DAI file_io_dai direction handling should be explicit');
assertIncludes("id === 'builtin.dai'", 'FILE_IO DAI should be represented as builtin.dai plus parameters');
assertIncludes("dai_type", 'DAI parameters should include dai_type');
assertIncludes("dai_index", 'DAI parameters should include dai_index');
assertIncludes('paramValues', 'DAI parameters should stay in the node params object');
assertNotIncludes("type_id: 'file_io_dai'", 'frontend should not introduce a standalone file_io_dai module type');
assertIncludes('updateDaiDirectionParam', 'DAI port side switch should update direction param');

assertIncludes('renamePipeline', 'single-pipeline toolbox should support rename');
assertIncludes('Build must succeed before Save', 'Save should reject until build succeeds');
assertIncludes("apiPost('/api/project/save'", 'successful Save should call backend save');

console.log('gui-runtime-contract.test passed');
