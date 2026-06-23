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
for (const state of ['NOT_READY', 'PIPE_LOADED', 'RUNNING', 'ERROR']) {
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
assertIncludes('isParamEnabledForNode', 'Inspector controls should be gated by runtime state');
assertIncludes('writeInspectorPresetParam', 'Inspector changes should write to inspector_preset');
assertIncludes('data-param-disabled', 'Inspector disabled state should be reflected in DOM');

assertIncludes('debug_file_io', 'snapshot should carry debug file I/O outside as_config payload');
assertIncludes('working_groups', 'snapshot should send working groups for backend pipeline regeneration');
assertIncludes('pipelineNodeId', 'snapshot should send original/local pipeline node ids');
assertIncludes('inst_ref', 'snapshot should send module instance references for HOST/DAI');
assertIncludes('port_domains', 'snapshot should send per-port SOF/external domains');
assertIncludes('data-file-param-action', 'file_open/file_save controls should be rendered');
assertIncludes("t === 'file_io'", 'Inspector should map file_io value_type to file open/save controls');
assertIncludes('readAudioFileInfo', 'WAV selection should parse basic audio info');
assertIncludes('isDebugFileIoNode', 'debug file I/O nodes should be identifiable');
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
assertIncludes("file_path", 'FILE_IO DAI should expose a file path parameter');
assertNotIncludes("type_id: 'file_io_dai'", 'frontend should not introduce a standalone file_io_dai module type');
assertIncludes('updateDaiDirectionParam', 'DAI port side switch should update direction param');

assertIncludes('renamePipeline', 'single-pipeline toolbox should support rename');
assertIncludes('Build must succeed before Save', 'Save should reject until build succeeds');
assertIncludes("apiPost('/api/project/save'", 'successful Save should call backend save');

console.log('gui-runtime-contract.test passed');
