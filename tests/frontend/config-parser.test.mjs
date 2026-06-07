import assert from 'node:assert/strict';
import fs from 'node:fs';
import { buildRegistry, convertPipeline } from '../../frontend/assets/js/configParser.js';

const cfg = JSON.parse(fs.readFileSync(new URL('../../config/A2.json', import.meta.url), 'utf8'));
const registry = buildRegistry(cfg);

// Product JSON module types plus Audio Studio built-in virtual I/O modules:
//   virtual.file_input, virtual.mic_input, virtual.audio_output
assert.equal(registry.moduleTypes.size, cfg.module_types.length + 3);
assert.ok(registry.moduleTypes.has('virtual.file_input'));
assert.ok(registry.moduleTypes.has('virtual.mic_input'));
assert.ok(registry.moduleTypes.has('virtual.audio_output'));
assert.equal(registry.moduleTypes.get('virtual.file_input').category, 'INPUT / OUTPUT');
assert.equal(registry.moduleTypes.get('virtual.mic_input').category, 'INPUT / OUTPUT');
assert.equal(registry.moduleTypes.get('virtual.audio_output').category, 'INPUT / OUTPUT');

assert.equal(registry.instances.size, cfg.module_instances.length);
const playback = convertPipeline(cfg, 'PLAY_MAIN');
assert.ok(playback.nodes.length > 4);
assert.ok(playback.edges.length > 4);
const aec = convertPipeline(cfg, 'CAPTURE_VOICE').nodes.find(n => n.id === 'AEC');
assert.deepEqual(aec.inputs.map(p => p.name), ['mic', 'ref']);
assert.deepEqual(aec.outputs.map(p => p.name), ['out']);
assert.ok(Object.prototype.hasOwnProperty.call(aec.staticParams, 'tail_ms'));
assert.ok(Object.prototype.hasOwnProperty.call(aec.runtimeParams, 'echo_suppress_db'));
console.log('config-parser.test passed');
