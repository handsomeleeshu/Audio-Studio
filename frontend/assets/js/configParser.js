import { deepClone, getByPath, parseEndpoint, safeName } from './utils.js';

const VIRTUAL_IO_MODULE_TYPES = [
  {
    type_id: 'virtual.file_input',
    name: 'File Input',
    category: 'INPUT / OUTPUT',
    io: { in_ports: [], out_ports: [{ name: 'L', max_ch: 1 }, { name: 'R', max_ch: 1 }] },
    static_schema: { fields: [
      { key: 'source', type: 'enum', enum: ['local_file', 'demo_sine', 'demo_speech'], default: 'local_file' },
      { key: 'loop', type: 'bool', default: true }
    ] },
    runtime_params: [
      { param_id: 'gain', param_name: 'Gain', value_type: 'float', default: 0, unit: 'dB', range: { min: -60, max: 12, step: 0.5 } }
    ]
  },
  {
    type_id: 'virtual.mic_input',
    name: 'Mic Input',
    category: 'INPUT / OUTPUT',
    io: { in_ports: [], out_ports: [{ name: 'mic0', max_ch: 1 }, { name: 'mic1', max_ch: 1 }, { name: 'mic2', max_ch: 1 }, { name: 'mic3', max_ch: 1 }] },
    static_schema: { fields: [
      { key: 'device', type: 'enum', enum: ['browser_mic', 'virtual_mic_array'], default: 'browser_mic' },
      { key: 'channels', type: 'uint', default: 4, range: { min: 1, max: 8, step: 1 } }
    ] },
    runtime_params: [
      { param_id: 'pre_gain', param_name: 'Pre Gain', value_type: 'float', default: 0, unit: 'dB', range: { min: -20, max: 20, step: 0.5 } }
    ]
  },
  {
    type_id: 'virtual.audio_output',
    name: 'Audio Output',
    category: 'INPUT / OUTPUT',
    io: { in_ports: [{ name: 'L', max_ch: 1 }, { name: 'R', max_ch: 1 }], out_ports: [] },
    static_schema: { fields: [
      { key: 'sink', type: 'enum', enum: ['browser_playback', 'wav_render', 'virtual_speaker'], default: 'browser_playback' }
    ] },
    runtime_params: [
      { param_id: 'volume', param_name: 'Volume', value_type: 'float', default: 0, unit: 'dB', range: { min: -60, max: 12, step: 0.5 } },
      { param_id: 'mute', param_name: 'Mute', value_type: 'bool', default: false }
    ]
  }
];

function defaultPortForRole(role) {
  if (role === 'source') return { inputs: [], outputs: [{ name: 'out', max_ch: 32 }] };
  if (role === 'sink') return { inputs: [{ name: 'in', max_ch: 32 }], outputs: [] };
  return { inputs: [{ name: 'in', max_ch: 32 }], outputs: [{ name: 'out', max_ch: 32 }] };
}

function buildParamDefaults(moduleType, presetValues = {}) {
  const staticParams = {};
  const runtimeParams = {};
  for (const field of moduleType?.static_schema?.fields || []) {
    staticParams[field.key] = Object.prototype.hasOwnProperty.call(presetValues, field.key) ? presetValues[field.key] : field.default;
  }
  for (const param of moduleType?.runtime_params || []) {
    runtimeParams[param.param_id] = param.default;
  }
  return { staticParams, runtimeParams };
}

function normalizedLibraryCategory(moduleType) {
  const text = `${moduleType?.type_id || ''} ${moduleType?.category || ''} ${moduleType?.name || ''}`.toLowerCase();
  if (text.includes('virtual.') || text.includes('port.source') || text.includes('port.sink') || text.includes('file') || text.includes('mic_input') || text.includes('audio_output')) return 'INPUT / OUTPUT';
  if (text.includes('neural') || text.includes('npu') || text.includes('/ai') || text.endsWith(' ai') || text.includes(' ai.')) return 'AI';
  if (text.includes('aec') || text.includes('beam') || text.includes('noise') || text.includes('ns') || text.includes('agc') || text.includes('vad') || text.includes('kws') || text.includes('asr') || text.includes('voice')) return 'VOICE';
  if (text.includes('eq') || text.includes('drc') || text.includes('mixer') || text.includes('mix') || text.includes('volume') || text.includes('gain') || text.includes('loudness') || text.includes('fader') || text.includes('balance') || text.includes('virtual') || text.includes('playback/eq') || text.includes('playback/dynamics')) return 'EFFECTS';
  if (text.includes('src') || text.includes('asrc') || text.includes('delay') || text.includes('route') || text.includes('mux') || text.includes('filter') || text.includes('resampler') || text.includes('graph/basic') || text.includes('graph/routing') || text.includes('common/filter')) return 'UTILITIES';
  return String(moduleType?.category || 'UTILITIES').toUpperCase();
}

function normalizeModuleTypeForLibrary(moduleType) {
  return { ...moduleType, category: normalizedLibraryCategory(moduleType) };
}

export function buildRegistry(productConfig) {
  const moduleTypes = new Map();
  const instances = new Map();

  // Audio Studio front-end virtual I/O controls are always available.
  // They are intentionally independent from product JSON so a project can always
  // use local file input, browser microphone input, and browser/output render nodes.
  for (const mt of VIRTUAL_IO_MODULE_TYPES) moduleTypes.set(mt.type_id, deepClone(mt));

  for (const mt of productConfig.module_types || []) {
    moduleTypes.set(mt.type_id, normalizeModuleTypeForLibrary(mt));
  }
  for (const inst of productConfig.module_instances || []) instances.set(inst.inst_id, inst);
  return { moduleTypes, instances };
}

export function resolveModuleForNode(productConfig, registry, node) {
  if (node.kind === 'module') {
    const inst = registry.instances.get(node.inst_ref);
    const moduleType = registry.moduleTypes.get(inst?.module_type);
    const preset = getByPath(productConfig, inst?.static_preset);
    return { inst, moduleType, displayName: inst?.name || node.node_id, presetValues: preset?.values || {} };
  }
  if (node.kind === 'module_inline') {
    const moduleType = registry.moduleTypes.get(node.inline?.module_type);
    return { inst: null, moduleType, displayName: node.inline?.name || moduleType?.name || node.node_id, presetValues: {} };
  }
  return { inst: null, moduleType: null, displayName: node.node_id, presetValues: {} };
}

export function convertPipeline(productConfig, pipeId) {
  const registry = buildRegistry(productConfig);
  const pipe = (productConfig.pipelines || []).find(p => p.pipe_id === pipeId) || productConfig.pipelines?.[0];
  if (!pipe) throw new Error('No pipeline in product config');
  const portMap = new Map((pipe.ports || []).map(p => [p.port_id, p]));
  const nodes = [];
  for (const raw of pipe.nodes || []) {
    const { inst, moduleType, displayName, presetValues } = resolveModuleForNode(productConfig, registry, raw);
    let inputs = [];
    let outputs = [];
    let category = 'port';
    let moduleTypeId = 'port';
    let subtitle = 'pipeline port';
    if (raw.kind === 'port') {
      const port = portMap.get(raw.port_ref);
      const ports = defaultPortForRole(port?.role);
      inputs = ports.inputs;
      outputs = ports.outputs;
      category = `port/${port?.role || 'io'}`;
      moduleTypeId = `port.${port?.role || 'io'}`;
      subtitle = `${port?.role || 'port'} · ${port?.hw?.id || raw.port_ref || ''}`;
    } else {
      inputs = moduleType?.io?.in_ports || [];
      outputs = moduleType?.io?.out_ports || [];
      category = moduleType?.category || 'module';
      moduleTypeId = moduleType?.type_id || 'unknown';
      subtitle = moduleTypeId;
    }
    const defaults = buildParamDefaults(moduleType, presetValues);
    nodes.push({
      id: raw.node_id,
      kind: raw.kind,
      title: displayName,
      instId: raw.inst_ref || null,
      moduleTypeId,
      category,
      subtitle,
      raw: deepClone(raw),
      moduleType: deepClone(moduleType),
      inputs: inputs.map((p, i) => ({ id: safeName(p.name || `in${i}`), name: p.name || `in${i}`, maxCh: p.max_ch || 0 })),
      outputs: outputs.map((p, i) => ({ id: safeName(p.name || `out${i}`), name: p.name || `out${i}`, maxCh: p.max_ch || 0 })),
      staticParams: defaults.staticParams,
      runtimeParams: defaults.runtimeParams,
      core: moduleType?.service_binding?.preferred_core ? Number(String(moduleType.service_binding.preferred_core).replace('core', '')) : 0,
      x: 0,
      y: 0,
      cost: { cpu: 0, memKb: 0, latencyMs: 0, rms: -60, peak: -60 }
    });
  }
  const edges = (pipe.edges || []).map((e, idx) => {
    const from = parseEndpoint(e.from);
    const to = parseEndpoint(e.to);
    return { id: `edge_${idx}_${from.nodeId}_${to.nodeId}`, from, to };
  });
  return { pipe: deepClone(pipe), nodes, edges, registry };
}

export function makeNodeFromModuleType(moduleType, x = 100, y = 100) {
  const defaults = buildParamDefaults(moduleType, {});
  const title = moduleType.name || moduleType.display_name || moduleType.type_id.split('.').pop();
  const id = safeName(`${moduleType.type_id.split('.').pop().toUpperCase()}_${Math.random().toString(16).slice(2, 6)}`);
  return {
    id,
    kind: 'module_inline',
    title,
    instId: null,
    moduleTypeId: moduleType.type_id,
    category: moduleType.category || 'module',
    subtitle: moduleType.type_id,
    raw: { node_id: id, kind: 'module_inline', inline: { module_type: moduleType.type_id, name: title } },
    moduleType: deepClone(moduleType),
    inputs: (moduleType.io?.in_ports || []).map((p, i) => ({ id: safeName(p.name || `in${i}`), name: p.name || `in${i}`, maxCh: p.max_ch || 0 })),
    outputs: (moduleType.io?.out_ports || []).map((p, i) => ({ id: safeName(p.name || `out${i}`), name: p.name || `out${i}`, maxCh: p.max_ch || 0 })),
    staticParams: defaults.staticParams,
    runtimeParams: defaults.runtimeParams,
    core: 0,
    x,
    y,
    cost: { cpu: 0, memKb: 0, latencyMs: 0, rms: -60, peak: -60 }
  };
}

export function exportPipelineJson(state) {
  return {
    pipe_id: state.currentPipeId,
    name: state.pipeline?.name || state.currentPipeId,
    nodes: state.nodes.map(n => ({
      node_id: n.id,
      kind: n.kind,
      module_type: n.moduleTypeId,
      inst_ref: n.instId,
      title: n.title,
      x: Math.round(n.x),
      y: Math.round(n.y),
      core: n.core,
      static_params: n.staticParams,
      runtime_params: n.runtimeParams
    })),
    edges: state.edges.map(e => ({ from: `${e.from.nodeId}:${e.from.portName}`, to: `${e.to.nodeId}:${e.to.portName}` }))
  };
}
