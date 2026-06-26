import { deepClone, parseEndpoint, safeName } from './utils.js';

function buildParamDefaults(moduleType, presetValues = {}) {
  const staticParams = {};
  const runtimeParams = {};
  const unifiedParams = moduleType ? moduleType.parameters || [] : [];
  const canSetWhileRunning = param => {
    const states = param.apply ? param.apply.settable_states || [] : [];
    return states.indexOf('PIPE_RUNNING') >= 0;
  };
  const staticFields = unifiedParams.filter(param => {
    return !canSetWhileRunning(param);
  });
  const runtimeFields = unifiedParams.filter(param => {
    return canSetWhileRunning(param);
  });
  for (const field of staticFields) {
    const key = field.key || field.param_id;
    if (!key) continue;
    staticParams[key] = Object.prototype.hasOwnProperty.call(presetValues, key) ? presetValues[key] : field.default;
  }
  for (const param of runtimeFields) {
    const key = param.param_id || param.key;
    if (!key) continue;
    runtimeParams[key] = param.default;
  }
  return { staticParams, runtimeParams };
}

function endpointNodePorts(params, moduleTypeId, fallbackInputs, fallbackOutputs) {
  const maxCh = Number(params && (params.channels_max || params.channels) || 2);
  const direction = String(params && params.direction || '').toLowerCase();
  if (moduleTypeId === 'builtin.host' || moduleTypeId === 'host') {
    const playback = direction !== 'capture';
    return {
      inputs: [{ name: 'in', max_ch: maxCh, domain: playback ? 'external' : 'sof', external: playback }],
      outputs: [{ name: 'out', max_ch: maxCh, domain: playback ? 'sof' : 'external', external: !playback }]
    };
  }
  if (moduleTypeId === 'builtin.dai' || moduleTypeId === 'dai') {
    const hasInput = direction !== 'capture';
    return {
      inputs: hasInput ? [{ name: 'in', max_ch: maxCh, domain: 'sof' }] : [],
      outputs: hasInput ? [] : [{ name: 'out', max_ch: maxCh, domain: 'sof' }]
    };
  }
  return { inputs: fallbackInputs, outputs: fallbackOutputs };
}

function portDomain(port) {
  return String(port && (port.domain || (port.external ? 'external' : 'sof')) || 'sof').toLowerCase() === 'external'
    ? 'external'
    : 'sof';
}

function convertPort(port, dir, index) {
  const name = port && port.name || `${dir}${index}`;
  const domain = portDomain(port);
  return {
    id: safeName(name),
    name,
    maxCh: port && (port.max_ch || port.maxCh) || 0,
    domain,
    external: domain === 'external'
  };
}

function normalizedLibraryCategory(moduleType) {
  const text = `${moduleType && moduleType.type_id || ''} ${moduleType && moduleType.category || ''} ${moduleType && moduleType.name || ''}`.toLowerCase();
  if (text.includes('port.source') || text.includes('port.sink') || text.includes('file') || text.includes('mic_input') || text.includes('audio_output')) return 'INPUT / OUTPUT';
  if (text.includes('neural') || text.includes('npu') || text.includes('/ai') || text.endsWith(' ai') || text.includes(' ai.')) return 'AI';
  if (text.includes('aec') || text.includes('beam') || text.includes('noise') || text.includes('ns') || text.includes('agc') || text.includes('vad') || text.includes('kws') || text.includes('asr') || text.includes('voice')) return 'VOICE';
  if (text.includes('eq') || text.includes('drc') || text.includes('mixer') || text.includes('mix') || text.includes('volume') || text.includes('gain') || text.includes('loudness') || text.includes('fader') || text.includes('balance') || text.includes('playback/eq') || text.includes('playback/dynamics')) return 'EFFECTS';
  if (text.includes('src') || text.includes('asrc') || text.includes('delay') || text.includes('route') || text.includes('mux') || text.includes('filter') || text.includes('resampler') || text.includes('graph/basic') || text.includes('graph/routing') || text.includes('common/filter')) return 'UTILITIES';
  return String(moduleType && moduleType.category || 'UTILITIES').toUpperCase();
}

function normalizeModuleTypeForLibrary(moduleType) {
  return { ...moduleType, category: normalizedLibraryCategory(moduleType) };
}

function importedCatalogModuleTypes(options = {}) {
  const catalogs = Array.isArray(options.catalogs) ? options.catalogs : [];
  return catalogs.flatMap(catalog => Array.isArray(catalog && catalog.module_types) ? catalog.module_types : []);
}

function addModuleType(moduleTypes, moduleType, source) {
  if (!moduleType || !moduleType.type_id) return;
  if (moduleTypes.has(moduleType.type_id)) {
    throw new Error(`Duplicate module_type ${moduleType.type_id} from ${source}`);
  }
  moduleTypes.set(moduleType.type_id, normalizeModuleTypeForLibrary(moduleType));
}

export function buildRegistry(productConfig, options = {}) {
  const moduleTypes = new Map();

  for (const mt of importedCatalogModuleTypes(options)) addModuleType(moduleTypes, mt, 'imported catalog');

  for (const mt of productConfig.module_types || []) {
    addModuleType(moduleTypes, mt, 'project config');
  }
  return { moduleTypes };
}

export function resolveModuleForNode(productConfig, registry, node) {
  const moduleType = registry.moduleTypes.get(node.module_type);
  return {
    moduleType,
    displayName: node.name || moduleType && (moduleType.name || moduleType.ui && moduleType.ui.display_name) || node.node_id,
    presetValues: node.params || {}
  };
}

function inferFrontendDirection(spec, moduleTypeId) {
  const explicit = String(spec && spec.direction || '').toLowerCase();
  if (explicit === 'input' || explicit === 'output') return explicit;
  const typeText = String(moduleTypeId || '').toLowerCase();
  if (typeText.includes('output')) return 'output';
  return 'input';
}

function convertFrontendNode(registry, spec, index) {
  const nodeId = spec.node_id || spec.id || `FRONTEND_${index + 1}`;
  const moduleTypeId = spec.module_type || spec.moduleType;
  const direction = inferFrontendDirection(spec, moduleTypeId);
  const moduleType = registry.moduleTypes.get(moduleTypeId);
  const defaults = buildParamDefaults(moduleType, spec.params || {});
  const staticParams = {
    ...defaults.staticParams,
    ...(spec.params || {})
  };
  const inputs = moduleType && moduleType.io ? moduleType.io.in_ports || [] : [];
  const outputs = moduleType && moduleType.io ? moduleType.io.out_ports || [] : [];
  return {
    id: nodeId,
    kind: 'frontend',
    title: spec.name || spec.title || moduleType && moduleType.name || nodeId,
    moduleTypeId,
    category: moduleType && moduleType.category || 'INPUT / OUTPUT',
    subtitle: direction,
    raw: deepClone(spec),
    moduleType: deepClone(moduleType),
    inputs: inputs.map((p, i) => convertPort(p, 'in', i)),
    outputs: outputs.map((p, i) => convertPort(p, 'out', i)),
    staticParams,
    runtimeParams: defaults.runtimeParams,
    core: 0,
    x: Number(spec.ui && spec.ui.x || 0),
    y: Number(spec.ui && spec.ui.y || 0),
    cost: { cpu: 0, memKb: 0, latencyMs: 0, rms: -60, peak: -60 },
    frontendConnection: deepClone(spec)
  };
}

function frontendConnectionsForPipeline(productConfig, pipeId) {
  const connections = Array.isArray(productConfig.frontend_connections) ? productConfig.frontend_connections : [];
  return connections.filter(conn => conn && conn.pipeline_id === pipeId);
}

export function convertPipeline(productConfig, pipeId, options = {}) {
  const registry = buildRegistry(productConfig, options);
  const pipelines = productConfig.pipelines || [];
  const pipe = pipelines.find(p => p.pipe_id === pipeId) || pipelines[0];
  if (!pipe) throw new Error('No pipeline in product config');
  const nodes = [];
  for (const raw of pipe.nodes || []) {
    const { moduleType, displayName, presetValues } = resolveModuleForNode(productConfig, registry, raw);
    let inputs = moduleType && moduleType.io ? moduleType.io.in_ports || [] : [];
    let outputs = moduleType && moduleType.io ? moduleType.io.out_ports || [] : [];
    const moduleTypeId = moduleType && moduleType.type_id || raw.module_type || 'unknown';
    const endpointPorts = endpointNodePorts(raw.params || {}, moduleTypeId, inputs, outputs);
    inputs = endpointPorts.inputs;
    outputs = endpointPorts.outputs;
    const defaults = buildParamDefaults(moduleType, presetValues);
    nodes.push({
      id: raw.node_id,
      kind: 'module',
      title: displayName,
      moduleTypeId,
      category: moduleType && moduleType.category || 'module',
      subtitle: moduleTypeId,
      raw: deepClone(raw),
      moduleType: deepClone(moduleType),
      inputs: inputs.map((p, i) => convertPort(p, 'in', i)),
      outputs: outputs.map((p, i) => convertPort(p, 'out', i)),
      staticParams: defaults.staticParams,
      runtimeParams: defaults.runtimeParams,
      core: moduleType && moduleType.service_binding && moduleType.service_binding.preferred_core ? Number(String(moduleType.service_binding.preferred_core).replace('core', '')) : 0,
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
  const frontendConnections = frontendConnectionsForPipeline(productConfig, pipe.pipe_id);
  const frontendNodes = frontendConnections.flatMap((conn, connIdx) =>
    (conn.nodes || []).map((node, nodeIdx) => convertFrontendNode(registry, node, connIdx + nodeIdx)));
  const frontendEdges = frontendConnections.flatMap((conn, connIdx) => (conn.edges || []).map((edge, edgeIdx) => {
    const from = parseEndpoint(edge.from);
    const to = parseEndpoint(edge.to);
    return {
      id: `frontend_edge_${connIdx}_${edgeIdx}_${from.nodeId}_${to.nodeId}`,
      from,
      to
    };
  }));
  return { pipe: deepClone(pipe), nodes, edges, frontendNodes, frontendEdges, registry };
}

export function makeNodeFromModuleType(moduleType, x = 100, y = 100) {
  const defaults = buildParamDefaults(moduleType, {});
  const title = moduleType.name || moduleType.display_name || moduleType.type_id.split('.').pop();
  const id = safeName(`${moduleType.type_id.split('.').pop().toUpperCase()}_${Math.random().toString(16).slice(2, 6)}`);
  return {
    id,
    kind: 'module',
    title,
    moduleTypeId: moduleType.type_id,
    category: moduleType.category || 'module',
    subtitle: moduleType.type_id,
    raw: { node_id: id, name: title, module_type: moduleType.type_id, params: defaults.staticParams },
    moduleType: deepClone(moduleType),
    inputs: (moduleType.io && moduleType.io.in_ports || []).map((p, i) => convertPort(p, 'in', i)),
    outputs: (moduleType.io && moduleType.io.out_ports || []).map((p, i) => convertPort(p, 'out', i)),
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
    name: state.pipeline && state.pipeline.name || state.currentPipeId,
    nodes: state.nodes.map(n => ({
      node_id: n.id,
      name: n.title,
      module_type: n.moduleTypeId,
      x: Math.round(n.x),
      y: Math.round(n.y),
      core: n.core,
      params: n.staticParams,
      runtime_params: n.runtimeParams
    })),
    edges: state.edges.map(e => ({ from: `${e.from.nodeId}:${e.from.portName}`, to: `${e.to.nodeId}:${e.to.portName}` }))
  };
}
