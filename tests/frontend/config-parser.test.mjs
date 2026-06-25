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
assert.ok(Array.isArray(cfg.frontend_connections), 'A2.json must persist frontend File I/O connections outside pipelines[]');
assert.equal(cfg.frontend_connections.length, 3, 'A2.json must describe the playback, capture, and DSP coverage frontend links');

assert.equal(registry.instances.size, cfg.module_instances.length);
const playback = convertPipeline(cfg, 'PLAYBACK_MAIN', { catalogs: [builtin] });
assert.ok(playback.nodes.length > 4);
assert.ok(playback.edges.length > 4);
assert.equal(playback.frontendNodes.length, 1);
assert.equal(playback.frontendEdges.length, 1);
assert.equal(playback.frontendNodes[0].id, 'FILE_IN');
assert.equal(playback.frontendNodes[0].moduleTypeId, 'virtual.file_input');
assert.equal(playback.frontendNodes[0].staticParams.host_stream, 'as_config_playback');
assert.equal(playback.frontendEdges[0].from.nodeId, 'FILE_IN');
assert.equal(playback.frontendEdges[0].from.portName, 'L');
assert.equal(playback.frontendEdges[0].to.nodeId, 'HOST_IN');
assert.equal(playback.frontendEdges[0].to.portName, 'in');
const playbackHost = playback.nodes.find(n => n.id === 'HOST_IN');
assert.deepEqual(playbackHost.inputs.map(p => p.name), ['in']);
assert.deepEqual(playbackHost.outputs.map(p => p.name), ['out']);
assert.equal(playbackHost.inputs.find(p => p.name === 'in').domain, 'external');
assert.equal(playbackHost.outputs.find(p => p.name === 'out').domain, 'sof');
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
assert.deepEqual(captureHost.outputs.map(p => p.name), ['out']);
assert.equal(captureHost.inputs.find(p => p.name === 'in').domain, 'sof');
assert.equal(captureHost.outputs.find(p => p.name === 'out').domain, 'external');
assert.equal(capture.frontendNodes.length, 1);
assert.equal(capture.frontendEdges.length, 1);
assert.equal(capture.frontendNodes[0].id, 'FILE_OUT');
assert.equal(capture.frontendNodes[0].moduleTypeId, 'virtual.audio_output');
assert.equal(capture.frontendNodes[0].staticParams.host_stream, 'as_config_capture');
assert.equal(capture.frontendEdges[0].from.nodeId, 'HOST_OUT');
assert.equal(capture.frontendEdges[0].from.portName, 'out');
assert.equal(capture.frontendEdges[0].to.nodeId, 'FILE_OUT');
assert.equal(capture.frontendEdges[0].to.portName, 'L');
console.log('config-parser.test passed');
