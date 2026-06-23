import { strict as assert } from 'assert';
import fs from 'fs';
import { buildRegistry, convertPipeline } from '../../GUI/frontend/assets/js/configParser.js';

const cfg = JSON.parse(fs.readFileSync(new URL('../../configs/platform/a2/A2.json', import.meta.url), 'utf8'));
const builtin = JSON.parse(fs.readFileSync(new URL('../../configs/built-in-algorithm.json', import.meta.url), 'utf8'));
const registry = buildRegistry(cfg, { catalogs: [builtin] });

assert.ok(Array.isArray(cfg.imports), 'A2.json must explicitly import module catalogs');
assert.ok(
  cfg.imports.some(item => item.kind === 'module_catalog' && item.path === 'configs/built-in-algorithm.json'),
  'A2.json must import the built-in algorithm catalog'
);
assert.equal(
  registry.moduleTypes.size,
  cfg.module_types.length + builtin.module_types.length + 3,
  'registry must include product types, imported built-in types, and virtual I/O types'
);

// Imported module types plus Audio Studio built-in virtual I/O modules:
//   virtual.file_input, virtual.mic_input, virtual.audio_output
assert.ok(registry.moduleTypes.has('virtual.file_input'));
assert.ok(registry.moduleTypes.has('virtual.mic_input'));
assert.ok(registry.moduleTypes.has('virtual.audio_output'));
assert.equal(registry.moduleTypes.get('virtual.file_input').category, 'INPUT / OUTPUT');
assert.equal(registry.moduleTypes.get('virtual.mic_input').category, 'INPUT / OUTPUT');
assert.equal(registry.moduleTypes.get('virtual.audio_output').category, 'INPUT / OUTPUT');
assert.ok(registry.moduleTypes.has('filter.channel_remap'));
assert.ok(registry.moduleTypes.has('filter.dsp_filter'));
assert.ok(registry.moduleTypes.has('gain.volume'));
assert.equal((cfg.module_types || []).some(mt => mt.type_id === 'filter.channel_remap'), false);
assert.equal((cfg.module_types || []).some(mt => mt.type_id === 'filter.dsp_filter'), false);

assert.equal(registry.instances.size, cfg.module_instances.length);
const playback = convertPipeline(cfg, 'PLAYBACK_MAIN', { catalogs: [builtin] });
assert.ok(playback.nodes.length > 4);
assert.ok(playback.edges.length > 4);
const playbackHost = playback.nodes.find(n => n.id === 'HOST_IN');
assert.deepEqual(playbackHost.inputs.map(p => p.name), []);
assert.deepEqual(playbackHost.outputs.map(p => p.name), ['out']);
assert.equal(playbackHost.staticParams.stream_name, 'as_config_playback');
const playbackDai = playback.nodes.find(n => n.id === 'DAI_OUT');
assert.equal(playbackDai.moduleTypeId, 'builtin.dai');
assert.deepEqual(playbackDai.inputs.map(p => p.name), ['in']);
assert.deepEqual(playbackDai.outputs.map(p => p.name), []);
assert.equal(playbackDai.staticParams.dai_type, 'file_io_dai');
assert.equal(playbackDai.staticParams.dai_index, 0);
assert.equal(playbackDai.staticParams.direction, 'playback');
assert.equal(playbackDai.moduleType.parameters.find(p => p.param_id === 'file_path').value_type, 'file_io');
const chremap = playback.nodes.find(n => n.id === 'CHREMAP');
assert.deepEqual(chremap.inputs.map(p => p.name), ['in']);
assert.deepEqual(chremap.outputs.map(p => p.name), ['out']);
assert.ok(Object.prototype.hasOwnProperty.call(chremap.runtimeParams, 'layout'));
assert.equal(chremap.runtimeParams.layout, 'stereo_passthrough');
assert.equal(chremap.moduleType.parameters.find(p => p.param_id === 'layout').value_type, 'enum');
const fader = playback.nodes.find(n => n.id === 'FADER');
assert.ok(Object.prototype.hasOwnProperty.call(fader.runtimeParams, 'balance'));
assert.equal(fader.moduleType.parameters.find(p => p.param_id === 'balance').value_type, 'float');
const dspFilter = convertPipeline(cfg, 'DSP_FILTER_COVERAGE', { catalogs: [builtin] }).nodes.find(n => n.id === 'DSP_FILTER');
assert.deepEqual(dspFilter.inputs.map(p => p.name), ['in']);
assert.deepEqual(dspFilter.outputs.map(p => p.name), ['out']);
assert.equal(dspFilter.runtimeParams.filter_preset, 'passthrough');
assert.equal(dspFilter.moduleType.parameters.find(p => p.param_id === 'filter_preset').value_type, 'enum');
const capture = convertPipeline(cfg, 'CAPTURE_MAIN', { catalogs: [builtin] });
const captureDai = capture.nodes.find(n => n.id === 'DAI_IN');
assert.deepEqual(captureDai.inputs.map(p => p.name), []);
assert.deepEqual(captureDai.outputs.map(p => p.name), ['out']);
assert.equal(captureDai.staticParams.direction, 'capture');
const captureHost = capture.nodes.find(n => n.id === 'HOST_OUT');
assert.deepEqual(captureHost.inputs.map(p => p.name), ['in']);
assert.deepEqual(captureHost.outputs.map(p => p.name), []);
console.log('config-parser.test passed');
