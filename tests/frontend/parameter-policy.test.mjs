import { strict as assert } from 'assert';
import fs from 'fs';
import { convertPipeline } from '../../GUI/frontend/assets/js/configParser.js';
const cfg = JSON.parse(fs.readFileSync(new URL('../../configs/platform/a2/A2.json', import.meta.url), 'utf8'));
const builtin = JSON.parse(fs.readFileSync(new URL('../../configs/built-in-algorithm.json', import.meta.url), 'utf8'));
const indexHtml = fs.readFileSync(new URL('../../GUI/frontend/index.html', import.meta.url), 'utf8');
const graph = convertPipeline(cfg, 'PLAYBACK_MAIN', { catalogs: [builtin] });

function assertNoKey(value, key, path = '$') {
  if (!value || typeof value !== 'object') return;
  if (Object.prototype.hasOwnProperty.call(value, key)) {
    assert.fail(`${path} must not declare ${key}`);
  }
  if (Array.isArray(value)) {
    value.forEach((item, index) => assertNoKey(item, key, `${path}[${index}]`));
  } else {
    Object.entries(value).forEach(([childKey, child]) => assertNoKey(child, key, `${path}.${childKey}`));
  }
}

assertNoKey(builtin, 'kcontrol');
assert.equal(indexHtml.includes('p.kcontrol'), false, 'Inspector UI must not depend on kcontrol metadata');
assert.ok(indexHtml.includes('function displayIconForModuleIcon'), 'Library UI must map ui.icon keys to display glyphs');
assert.equal(indexHtml.includes('icon: ui.icon || iconForModule'), false, 'Library UI must not render ui.icon keys as raw text');
assert.ok(indexHtml.includes('const RUNTIME_STATES = Object.freeze({'), 'frontend should expose canonical runtime states');
assert.ok(indexHtml.includes('PIPE_LOADED'), 'build success should expose PIPE_LOADED instead of built');
assert.equal(indexHtml.includes('validated: { label:'), false, 'validated must not remain an exposed runtime state');
assert.equal(indexHtml.includes("built: { label:"), false, 'built must be mapped to PIPE_LOADED instead of exposed as built');
assert.equal(indexHtml.includes('stopped: { label:'), false, 'stopped must not remain an exposed runtime state');
assert.equal(/['"]RUNTIME['"]/.test(indexHtml), false, 'runtime gating must not use the legacy RUNTIME state token');
assert.ok(indexHtml.includes('ensureInspectorPreset'), 'Inspector should find/create inspector_preset');
assert.ok(indexHtml.includes("preset_id: 'inspector_preset'"), 'Inspector preset id should be stable');
assert.ok(indexHtml.includes('debug_file_io'), 'snapshot should carry debug file I/O outside as_config payload');
assert.ok(indexHtml.includes('applyBuildDiagnostics'), 'build diagnostics should mark nodes and ports');
assert.ok(indexHtml.includes("id === 'builtin.host'"), 'debug file I/O connection policy should recognize host modules');
assert.ok(indexHtml.includes('port-domain-external'), 'Host/debug external ports should use a solid visual marker');
assert.ok(indexHtml.includes('isExternalPort'), 'connection policy should distinguish external/debug ports');
assert.ok(indexHtml.includes("id === 'builtin.dai'"), 'FILE_IO DAI should be represented as builtin.dai plus parameters');

const volumeType = builtin.module_types.find(mt => mt.type_id === 'gain.volume');
assert.ok(volumeType, 'built-in catalog must expose real compile-time gain.volume type');
assert.equal(Array.isArray(volumeType.runtime_params), false, 'v2 built-in algorithms should not use runtime_params');
assert.ok(volumeType.parameters.some(p => p.param_id === 'volume_db' && p.value_type === 'float'));
assert.ok(volumeType.parameters.some(p => p.param_id === 'mute' && p.value_type === 'bool'));
assert.equal(volumeType.parameters.find(p => p.param_id === 'volume_db').range.min, -90);

const srcType = builtin.module_types.find(mt => mt.type_id === 'rate.src');
assert.ok(srcType.parameters.some(p => p.param_id === 'output_rate_hz' && p.value_type === 'enum'));
const hostType = builtin.module_types.find(mt => mt.type_id === 'builtin.host');
assert.ok(hostType.parameters.some(p => p.param_id === 'stream_name' && p.value_type === 'string'));
const daiType = builtin.module_types.find(mt => mt.type_id === 'builtin.dai');
assert.ok(daiType.parameters.some(p => p.param_id === 'dai_type' && p.value_type === 'enum'));
assert.ok(daiType.parameters.some(p => p.param_id === 'dai_index' && p.value_type === 'uint8'));
assert.ok(daiType.parameters.some(p => p.param_id === 'link_name' && p.value_type === 'string'));
assert.ok(daiType.parameters.some(p => p.param_id === 'tdm_slots' && p.value_type === 'uint8'));
const fileInputType = builtin.module_types.find(mt => mt.type_id === 'builtin.file_input');
const fileOutputType = builtin.module_types.find(mt => mt.type_id === 'builtin.file_output');
assert.ok(fileInputType.parameters.some(p => p.param_id === 'file_path' && p.value_type === 'file_io'));
assert.ok(fileOutputType.parameters.some(p => p.param_id === 'file_path' && p.value_type === 'file_save'));

const vol = graph.nodes.find(n => n.id === 'VOLUME');
assert.equal(vol.moduleType.parameters.find(p => p.param_id === 'volume_db').value_type, 'float');
assert.equal(vol.runtimeParams.volume_db, 0);

const capture = convertPipeline(cfg, 'CAPTURE_MAIN', { catalogs: [builtin] });
const src = capture.nodes.find(n => n.id === 'SRC');
assert.equal(src.moduleType.parameters.find(p => p.param_id === 'output_rate_hz').value_type, 'enum');
console.log('parameter-policy.test passed');
