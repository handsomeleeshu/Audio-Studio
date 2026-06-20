import { strict as assert } from 'assert';
import fs from 'fs';
import { buildRegistry, convertPipeline } from '../../GUI/frontend/assets/js/configParser.js';

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
const playback = convertPipeline(cfg, 'PLAYBACK_MAIN');
assert.ok(playback.nodes.length > 4);
assert.ok(playback.edges.length > 4);
const chremap = playback.nodes.find(n => n.id === 'CHREMAP');
assert.deepEqual(chremap.inputs.map(p => p.name), ['in']);
assert.deepEqual(chremap.outputs.map(p => p.name), ['out']);
assert.ok(Object.prototype.hasOwnProperty.call(chremap.runtimeParams, 'config'));
assert.equal(chremap.runtimeParams.config.mappings.length, 4);
const fader = playback.nodes.find(n => n.id === 'FADER');
assert.ok(Object.prototype.hasOwnProperty.call(fader.runtimeParams, 'config'));
assert.equal(fader.runtimeParams.config.ramp, 'linear');
const dspFilter = convertPipeline(cfg, 'DSP_FILTER_COVERAGE').nodes.find(n => n.id === 'DSP_FILTER');
assert.deepEqual(dspFilter.inputs.map(p => p.name), ['in']);
assert.deepEqual(dspFilter.outputs.map(p => p.name), ['out']);
assert.equal(dspFilter.runtimeParams.config.filters[0].filter_id, 'identity_fir');
console.log('config-parser.test passed');
