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

const volumeType = builtin.module_types.find(mt => mt.type_id === 'gain.volume');
assert.ok(volumeType, 'built-in catalog must expose real compile-time gain.volume type');
assert.equal(Array.isArray(volumeType.runtime_params), false, 'v2 built-in algorithms should not use runtime_params');
assert.ok(volumeType.parameters.some(p => p.param_id === 'volume_db' && p.value_type === 'float'));
assert.ok(volumeType.parameters.some(p => p.param_id === 'mute' && p.value_type === 'bool'));
assert.equal(volumeType.parameters.find(p => p.param_id === 'volume_db').range.min, -90);

const srcType = builtin.module_types.find(mt => mt.type_id === 'rate.src');
assert.ok(srcType.parameters.some(p => p.param_id === 'output_rate_hz' && p.value_type === 'enum'));

const vol = graph.nodes.find(n => n.id === 'VOLUME');
assert.equal(vol.moduleType.parameters.find(p => p.param_id === 'volume_db').value_type, 'float');
assert.equal(vol.runtimeParams.volume_db, 0);

const capture = convertPipeline(cfg, 'CAPTURE_MAIN', { catalogs: [builtin] });
const src = capture.nodes.find(n => n.id === 'SRC');
assert.equal(src.moduleType.parameters.find(p => p.param_id === 'output_rate_hz').value_type, 'enum');
console.log('parameter-policy.test passed');
