import { AudioStudioApi } from './api.js';
import { autoLayout, getNodeSize, getPortPosition, edgePath, viewportToWorld, WORLD_WIDTH, WORLD_HEIGHT } from './layout.js';
import { buildRegistry, convertPipeline, exportPipelineJson, makeNodeFromModuleType } from './configParser.js';
import { clamp, groupBy, nowTime, safeName } from './utils.js';
import { drawSpectrum, drawWaveform, makeMockTelemetry } from './telemetry.js';

const api = new AudioStudioApi('');
const els = {};
const state = {
  productConfig: null,
  registry: null,
  currentPipeId: null,
  pipeline: null,
  nodes: [],
  edges: [],
  selectedNodeId: null,
  runtime: 'stopped',
  buildStatus: 'ready',
  sessionId: null,
  camera: { x: 26, y: 34, zoom: 0.72 },
  drag: null,
  connecting: null,
  telemetry: null,
  eventLog: [],
  panels: { algorithmPanel: true, inspectorPanel: true, dashboardPanel: true },
  tabs: { inspector: 'runtime' },
  nextTelemetryAt: 0
};

function $(id) { return document.getElementById(id); }
function nodeById(id) { return state.nodes.find(n => n.id === id); }
function isRunning() { return state.runtime === 'running'; }
function isStopped() { return state.runtime === 'stopped'; }

window.addEventListener('DOMContentLoaded', init);

async function init() {
  Object.assign(els, {
    pipelineSelect: $('pipelineSelect'), sceneSelect: $('sceneSelect'), moduleLibrary: $('moduleLibrary'), moduleSearch: $('moduleSearch'),
    viewport: $('canvasViewport'), world: $('world'), edgeLayer: $('edgeLayer'), miniMap: $('miniMap'), inspector: $('inspector'),
    currentPipelineName: $('currentPipelineName'), pipelineMeta: $('pipelineMeta'), buildStatus: $('buildStatus'), warningStatus: $('warningStatus'), errorStatus: $('errorStatus'),
    runBadge: $('runBadge'), costTable: $('costTable'), coreLoading: $('coreLoading'), systemHealth: $('systemHealth'), meters: $('meters'), eventLog: $('eventLog'),
    waveCanvas: $('waveCanvas'), spectrumCanvas: $('spectrumCanvas'), modal: $('modal'), modalContent: $('modalContent')
  });
  document.body.classList.add('stopped');
  addLog('UI booting', 'Loading product JSON');
  state.productConfig = await api.getConfig();
  state.registry = buildRegistry(state.productConfig);
  populateSelectors();
  renderModuleLibrary();
  bindUi();
  loadPipeline(state.productConfig.pipelines[0].pipe_id);
  fitView(false);
  loop();
  addLog('Config loaded', `${state.productConfig.module_types.length} module types, ${state.productConfig.pipelines.length} pipelines`);
}

function populateSelectors() {
  els.pipelineSelect.innerHTML = '';
  for (const p of state.productConfig.pipelines || []) {
    const opt = document.createElement('option'); opt.value = p.pipe_id; opt.textContent = `${p.name} (${p.pipe_id})`; els.pipelineSelect.appendChild(opt);
  }
  els.sceneSelect.innerHTML = '<option value="">Manual</option>';
  for (const s of state.productConfig.scenes || []) {
    const opt = document.createElement('option'); opt.value = s.scene_id; opt.textContent = s.scene_id; els.sceneSelect.appendChild(opt);
  }
}

function bindUi() {
  els.pipelineSelect.addEventListener('change', () => {
    if (!isStopped()) return showToast('Running 时不能切换 pipeline，请先 Stop。');
    loadPipeline(els.pipelineSelect.value); fitView(false);
  });
  els.sceneSelect.addEventListener('change', () => {
    const scene = state.productConfig.scenes.find(s => s.scene_id === els.sceneSelect.value);
    if (scene?.active_pipelines?.[0]) { els.pipelineSelect.value = scene.active_pipelines[0]; loadPipeline(scene.active_pipelines[0]); fitView(false); }
  });
  $('validateBtn').addEventListener('click', validatePipeline);
  $('buildBtn').addEventListener('click', buildPipeline);
  $('runBtn').addEventListener('click', runPipeline);
  $('stopBtn').addEventListener('click', stopPipeline);
  $('arrangeBtn').addEventListener('click', () => { if (!isStopped()) return showToast('Run 状态下禁止重新布局。'); autoArrangeAndRender(); addLog('Auto Arrange', 'Applied minimum node spacing'); });
  $('deleteBtn').addEventListener('click', deleteSelectedNode);
  $('exportBtn').addEventListener('click', exportPipeline);
  $('docsBtn').addEventListener('click', () => showModal(`Audio Studio 二次开发入口\n\n1. 算法库来自 product JSON: module_types。\n2. 节点参数 UI 根据 static_schema/runtime_params 自动生成。\n3. 静态参数 only stopped，动态参数 running/stopped 均可修改。\n4. C++ 后端扩展接口：IRuntimeEngine / INodeController / IParameterController。\n5. Pipeline edge 格式：from node:output_port -> to node:input_port。`));
  $('modalClose').addEventListener('click', () => els.modal.classList.add('hidden'));
  $('zoomIn').addEventListener('click', () => setZoom(state.camera.zoom * 1.18));
  $('zoomOut').addEventListener('click', () => setZoom(state.camera.zoom / 1.18));
  $('zoomFit').addEventListener('click', () => fitView(true));
  $('audioFile').addEventListener('change', ev => { $('audioFileName').textContent = ev.target.files?.[0]?.name || 'No file selected'; addLog('Audio file selected', $('audioFileName').textContent); });
  els.moduleSearch.addEventListener('input', renderModuleLibrary);
  for (const btn of document.querySelectorAll('.panel-toggle')) btn.addEventListener('click', () => togglePanel(btn.dataset.panel, false));
  for (const btn of document.querySelectorAll('[data-open-panel]')) btn.addEventListener('click', () => togglePanel(btn.dataset.openPanel, true));
  els.viewport.addEventListener('dragover', ev => ev.preventDefault());
  els.viewport.addEventListener('drop', onDropModule);
  els.viewport.addEventListener('pointermove', onViewportPointerMove);
  els.viewport.addEventListener('pointerup', onViewportPointerUp);
  els.viewport.addEventListener('pointerleave', onViewportPointerUp);
  window.addEventListener('resize', () => { renderEdges(); renderMiniMap(); });
}

function loadPipeline(pipeId) {
  const converted = convertPipeline(state.productConfig, pipeId);
  state.currentPipeId = converted.pipe.pipe_id;
  state.pipeline = converted.pipe;
  state.nodes = converted.nodes;
  state.edges = converted.edges;
  state.selectedNodeId = state.nodes[0]?.id || null;
  autoLayout(state.nodes, state.edges);
  els.pipelineSelect.value = state.currentPipeId;
  els.currentPipelineName.textContent = converted.pipe.name || converted.pipe.pipe_id;
  els.pipelineMeta.textContent = `${converted.pipe.domain || 'domain'} · ${converted.pipe.frame?.rate || 48000} Hz · ${converted.pipe.frame?.block_ms || 10} ms block`;
  addLog('Pipeline loaded', `${converted.pipe.name || converted.pipe.pipe_id}`);
  renderAll();
}

function renderAll() {
  renderCamera();
  renderNodes();
  renderEdges();
  renderInspector();
  renderDashboard();
  renderMiniMap();
  renderRunState();
}

function renderModuleLibrary() {
  const filter = els.moduleSearch?.value?.toLowerCase() || '';
  const moduleTypes = Array.from(state.registry?.moduleTypes?.values?.() || []);
  const filtered = moduleTypes.filter(m => `${m.type_id} ${m.category}`.toLowerCase().includes(filter));
  const groups = groupBy(filtered, m => m.category || 'module');
  els.moduleLibrary.innerHTML = '';
  for (const [category, mods] of Array.from(groups.entries()).sort((a, b) => a[0].localeCompare(b[0]))) {
    const h = document.createElement('div'); h.className = 'module-category'; h.textContent = category; els.moduleLibrary.appendChild(h);
    for (const m of mods.sort((a,b) => a.type_id.localeCompare(b.type_id))) {
      const el = document.createElement('div'); el.className = 'module-item'; el.draggable = true; el.dataset.typeId = m.type_id;
      el.innerHTML = `<div class="name">${m.type_id}</div><div class="meta"><span>in:${m.io?.in_ports?.length || 0}</span><span>out:${m.io?.out_ports?.length || 0}</span><span>abi:${m.abi_version || '-'}</span></div>`;
      el.addEventListener('dragstart', ev => ev.dataTransfer.setData('text/module-type', m.type_id));
      els.moduleLibrary.appendChild(el);
    }
  }
}

function renderNodes() {
  els.world.innerHTML = '';
  for (const node of state.nodes) {
    const { w, h } = getNodeSize(node);
    const div = document.createElement('div');
    div.className = `node ${state.selectedNodeId === node.id ? 'selected' : ''} ${isRunning() ? 'running' : ''}`;
    div.dataset.nodeId = node.id;
    div.style.transform = `translate(${node.x}px, ${node.y}px)`;
    div.style.height = `${h}px`;
    div.innerHTML = `
      <button class="node-delete" title="Delete">×</button>
      <div class="node-header">
        <div><div class="node-title">${node.title}</div><div class="node-kind">${node.category}</div></div>
        <div class="node-kind">C${node.core ?? 0}</div>
      </div>
      <div class="node-meta">
        <span>CPU <b>${node.cost?.cpu?.toFixed?.(1) || '0.0'}%</b></span><span>MEM <b>${Math.round(node.cost?.memKb || 0)}KB</b></span>
        <span>LAT <b>${node.cost?.latencyMs?.toFixed?.(2) || '0.00'}ms</b></span><span>RMS <b>${node.cost?.rms || '-'}dB</b></span>
      </div>
    `;
    const inputs = document.createElement('div'); inputs.className = 'port-list inputs';
    for (const p of node.inputs) inputs.appendChild(makePortEl(node, p, 'input'));
    const outputs = document.createElement('div'); outputs.className = 'port-list outputs';
    for (const p of node.outputs) outputs.appendChild(makePortEl(node, p, 'output'));
    div.appendChild(inputs); div.appendChild(outputs);
    div.addEventListener('pointerdown', onNodePointerDown);
    div.addEventListener('click', async ev => {
      ev.stopPropagation();
      state.selectedNodeId = node.id; renderNodes(); renderInspector();
      api.nodeAction({ node_id: node.id, action: 'select', runtime: state.runtime });
    });
    div.querySelector('.node-delete').addEventListener('click', ev => { ev.stopPropagation(); state.selectedNodeId = node.id; deleteSelectedNode(); });
    els.world.appendChild(div);
  }
}

function makePortEl(node, port, direction) {
  const div = document.createElement('div');
  div.className = 'port'; div.dataset.nodeId = node.id; div.dataset.portName = port.name; div.dataset.direction = direction;
  const dot = document.createElement('span'); dot.className = 'port-dot';
  const label = document.createElement('span'); label.className = 'port-label'; label.textContent = port.name;
  if (direction === 'input') { div.appendChild(dot); div.appendChild(label); }
  else { div.appendChild(label); div.appendChild(dot); }
  div.addEventListener('pointerdown', onPortPointerDown);
  div.addEventListener('pointerup', onPortPointerUp);
  return div;
}

function renderEdges() {
  const marker = `<defs><marker id="arrow" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="4" markerHeight="4" orient="auto-start-reverse"><path d="M 0 0 L 10 5 L 0 10 z" fill="rgba(55,217,255,.78)"></path></marker></defs>`;
  els.edgeLayer.innerHTML = marker;
  const byId = new Map(state.nodes.map(n => [n.id, n]));
  for (const e of state.edges) {
    const fromNode = byId.get(e.from.nodeId), toNode = byId.get(e.to.nodeId);
    if (!fromNode || !toNode) continue;
    const p1 = getPortPosition(fromNode, 'output', e.from.portName);
    const p2 = getPortPosition(toNode, 'input', e.to.portName);
    const d = edgePath(p1, p2);
    const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    path.setAttribute('d', d); path.setAttribute('class', 'edge-path'); path.setAttribute('marker-end', 'url(#arrow)');
    els.edgeLayer.appendChild(path);
    const flow = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    flow.setAttribute('d', d); flow.setAttribute('class', 'edge-flow');
    els.edgeLayer.appendChild(flow);
  }
  if (state.connecting) {
    const fromNode = byId.get(state.connecting.from.nodeId);
    if (fromNode) {
      const p1 = getPortPosition(fromNode, 'output', state.connecting.from.portName);
      const p2 = state.connecting.to;
      const temp = document.createElementNS('http://www.w3.org/2000/svg', 'path');
      temp.setAttribute('d', edgePath(p1, p2)); temp.setAttribute('class', 'temp-edge');
      els.edgeLayer.appendChild(temp);
    }
  }
}

function renderCamera() {
  const t = `translate(${state.camera.x}px, ${state.camera.y}px) scale(${state.camera.zoom})`;
  els.world.style.transform = t;
  els.edgeLayer.style.transform = t;
  $('zoomText').textContent = `${Math.round(state.camera.zoom * 100)}%`;
}

function onNodePointerDown(ev) {
  if (!isStopped()) return;
  if (ev.target.closest('.port') || ev.target.closest('.node-delete')) return;
  const id = ev.currentTarget.dataset.nodeId;
  state.selectedNodeId = id; renderNodes(); renderInspector();
  state.drag = { nodeId: id, startX: ev.clientX, startY: ev.clientY, origX: nodeById(id).x, origY: nodeById(id).y };
  ev.currentTarget.setPointerCapture(ev.pointerId);
}

function onPortPointerDown(ev) {
  ev.stopPropagation();
  if (!isStopped()) return;
  const port = ev.currentTarget;
  if (port.dataset.direction !== 'output') return;
  const rect = els.viewport.getBoundingClientRect();
  state.connecting = {
    from: { nodeId: port.dataset.nodeId, portName: port.dataset.portName },
    to: viewportToWorld(ev.clientX, ev.clientY, rect, state.camera)
  };
  renderEdges();
}

function onPortPointerUp(ev) {
  ev.stopPropagation();
  if (!state.connecting || !isStopped()) return;
  const port = ev.currentTarget;
  if (port.dataset.direction !== 'input') return;
  const from = state.connecting.from;
  const to = { nodeId: port.dataset.nodeId, portName: port.dataset.portName };
  if (from.nodeId === to.nodeId) { state.connecting = null; renderEdges(); return showToast('不能连接到同一个节点。'); }
  const exists = state.edges.some(e => e.from.nodeId === from.nodeId && e.from.portName === from.portName && e.to.nodeId === to.nodeId && e.to.portName === to.portName);
  if (!exists) {
    state.edges.push({ id: `edge_${Date.now()}`, from, to });
    addLog('Edge connected', `${from.nodeId}:${from.portName} → ${to.nodeId}:${to.portName}`);
  }
  state.connecting = null; renderEdges(); renderMiniMap();
}

function onViewportPointerMove(ev) {
  if (state.drag) {
    const node = nodeById(state.drag.nodeId);
    node.x = clamp(state.drag.origX + (ev.clientX - state.drag.startX) / state.camera.zoom, 0, WORLD_WIDTH - 250);
    node.y = clamp(state.drag.origY + (ev.clientY - state.drag.startY) / state.camera.zoom, 0, WORLD_HEIGHT - 180);
    renderNodes(); renderEdges(); renderMiniMap();
  }
  if (state.connecting) {
    const rect = els.viewport.getBoundingClientRect();
    state.connecting.to = viewportToWorld(ev.clientX, ev.clientY, rect, state.camera);
    renderEdges();
  }
}

function onViewportPointerUp() {
  state.drag = null;
  if (state.connecting) { state.connecting = null; renderEdges(); }
}

function onDropModule(ev) {
  ev.preventDefault();
  if (!isStopped()) return showToast('Run 状态下禁止添加算法。');
  const typeId = ev.dataTransfer.getData('text/module-type');
  const mt = state.registry.moduleTypes.get(typeId);
  if (!mt) return;
  const pos = viewportToWorld(ev.clientX, ev.clientY, els.viewport.getBoundingClientRect(), state.camera);
  const node = makeNodeFromModuleType(mt, pos.x, pos.y);
  state.nodes.push(node);
  state.selectedNodeId = node.id;
  addLog('Node added', `${node.id} (${typeId})`);
  renderAll();
}

function autoArrangeAndRender() {
  autoLayout(state.nodes, state.edges);
  renderAll();
}

function deleteSelectedNode() {
  if (!isStopped()) return showToast('Run 状态下不能删除节点。');
  const id = state.selectedNodeId;
  if (!id) return;
  state.nodes = state.nodes.filter(n => n.id !== id);
  state.edges = state.edges.filter(e => e.from.nodeId !== id && e.to.nodeId !== id);
  state.selectedNodeId = state.nodes[0]?.id || null;
  addLog('Node deleted', id);
  renderAll();
}

function renderInspector() {
  const node = nodeById(state.selectedNodeId);
  if (!node) { els.inspector.className = 'inspector empty'; els.inspector.textContent = 'Select a node to configure.'; return; }
  els.inspector.className = 'inspector';
  const mt = node.moduleType || {};
  const tab = state.tabs.inspector || 'runtime';
  els.inspector.innerHTML = `
    <h3>${node.title}</h3>
    <div class="node-subtitle">${node.id} · ${node.subtitle} · ${node.category}</div>
    <div class="tabs">
      <button class="tab ${tab==='runtime'?'active':''}" data-tab="runtime">Dynamic</button>
      <button class="tab ${tab==='static'?'active':''}" data-tab="static">Static</button>
      <button class="tab ${tab==='mapping'?'active':''}" data-tab="mapping">Mapping</button>
      <button class="tab ${tab==='schema'?'active':''}" data-tab="schema">Schema</button>
    </div>
    <div id="runtimeTab" class="param-section ${tab==='runtime'?'active':''}"></div>
    <div id="staticTab" class="param-section ${tab==='static'?'active':''}"></div>
    <div id="mappingTab" class="param-section ${tab==='mapping'?'active':''}"></div>
    <div id="schemaTab" class="param-section ${tab==='schema'?'active':''}"><pre class="schema-block"></pre></div>
  `;
  for (const b of els.inspector.querySelectorAll('.tab')) b.addEventListener('click', () => { state.tabs.inspector = b.dataset.tab; renderInspector(); });
  renderParamRows($('runtimeTab'), node, mt.runtime_params || [], node.runtimeParams, false);
  renderStaticRows($('staticTab'), node, mt.static_schema?.fields || []);
  renderMapping($('mappingTab'), node);
  els.inspector.querySelector('.schema-block').textContent = JSON.stringify(mt || node.raw, null, 2);
}

function renderStaticRows(container, node, fields) {
  if (!fields.length) { container.innerHTML = '<div class="node-subtitle">No static parameters.</div>'; return; }
  const disabled = isRunning();
  container.innerHTML = '';
  for (const f of fields) {
    const row = makeParamRow({ id: f.key, label: f.key, valueType: f.type, enumValues: f.enum, range: f.range, unit: '', disabled, value: node.staticParams[f.key] }, value => {
      node.staticParams[f.key] = value; addLog('Static param changed', `${node.id}.${f.key}=${value}`);
    });
    if (disabled) row.classList.add('disabled');
    container.appendChild(row);
  }
  if (disabled) container.insertAdjacentHTML('afterbegin', '<div class="node-subtitle">Running 中静态参数锁定，请 Stop 后修改。</div>');
}

function renderParamRows(container, node, params, values) {
  if (!params.length) { container.innerHTML = '<div class="node-subtitle">No dynamic runtime parameters.</div>'; return; }
  container.innerHTML = '';
  for (const p of params) {
    const row = makeParamRow({ id: p.param_id, label: p.param_name || p.param_id, valueType: p.value_type, enumValues: p.enum || p.kcontrol?.enum_labels, range: p.range, unit: p.unit || '', disabled: false, value: values[p.param_id] }, async value => {
      values[p.param_id] = value;
      const resp = await api.updateParam({ session_id: state.sessionId, node_id: node.id, param_id: p.param_id, value, runtime: state.runtime, apply: p.apply || {} });
      addLog('Runtime param', `${node.id}.${p.param_id}=${value} ${resp?.ok ? 'OK' : 'FAIL'}`);
    });
    container.appendChild(row);
  }
}

function renderMapping(container, node) {
  container.innerHTML = `
    <div class="param-row"><div class="param-label"><span>DSP Core</span><small>editable stopped/running demo</small></div><div class="param-control"><select id="coreMapSelect"><option value="0">core0</option><option value="1">core1</option><option value="2">core2</option><option value="3">core3</option></select></div></div>
    <div class="param-row"><div class="param-label"><span>Ports</span><small>IO schema</small></div><pre class="schema-block">inputs: ${node.inputs.map(p=>p.name).join(', ') || '-'}\noutputs: ${node.outputs.map(p=>p.name).join(', ') || '-'}</pre></div>
  `;
  const select = container.querySelector('#coreMapSelect'); select.value = String(node.core || 0);
  select.addEventListener('change', () => { node.core = Number(select.value); addLog('Core mapping', `${node.id} -> core${node.core}`); renderNodes(); });
}

function makeParamRow(spec, onChange) {
  const row = document.createElement('div'); row.className = 'param-row';
  row.innerHTML = `<div class="param-label"><span>${spec.label}</span><small>${spec.valueType}${spec.unit ? ' · ' + spec.unit : ''}</small></div><div class="param-control"></div>`;
  const box = row.querySelector('.param-control');
  const type = spec.valueType;
  const disabled = !!spec.disabled;
  if (type === 'bool') {
    const sw = document.createElement('label'); sw.className = 'switch'; sw.innerHTML = `<input type="checkbox" ${spec.value ? 'checked' : ''} ${disabled ? 'disabled' : ''}/><span></span>`;
    sw.querySelector('input').addEventListener('change', ev => onChange(ev.target.checked)); box.appendChild(sw);
  } else if (type === 'enum') {
    const select = document.createElement('select'); select.disabled = disabled;
    for (const v of spec.enumValues || []) { const o = document.createElement('option'); o.value = v; o.textContent = v; select.appendChild(o); }
    select.value = spec.value;
    select.addEventListener('change', ev => onChange(ev.target.value)); box.appendChild(select);
  } else if (type === 'bytes') {
    const ta = document.createElement('textarea'); ta.rows = 4; ta.disabled = disabled; ta.value = typeof spec.value === 'string' ? spec.value : '<binary blob placeholder>'; ta.addEventListener('change', ev => onChange(ev.target.value)); box.appendChild(ta);
  } else if (spec.range || type?.startsWith('int') || type?.startsWith('uint') || type === 'float') {
    const range = spec.range || { min: 0, max: 100, step: 1 };
    const slider = document.createElement('input'); slider.type = 'range'; slider.min = range.min; slider.max = range.max; slider.step = range.step || 1; slider.value = spec.value ?? range.min; slider.disabled = disabled;
    const num = document.createElement('input'); num.type = 'number'; num.min = range.min; num.max = range.max; num.step = range.step || 1; num.value = spec.value ?? range.min; num.disabled = disabled;
    const sync = val => { const v = Number(val); slider.value = v; num.value = v; onChange(v); };
    slider.addEventListener('input', ev => { num.value = ev.target.value; });
    slider.addEventListener('change', ev => sync(ev.target.value)); num.addEventListener('change', ev => sync(ev.target.value));
    box.appendChild(slider); box.appendChild(num);
  } else {
    const input = document.createElement('input'); input.value = spec.value ?? ''; input.disabled = disabled; input.addEventListener('change', ev => onChange(ev.target.value)); box.appendChild(input);
  }
  return row;
}

function renderDashboard() {
  const t = state.telemetry || makeMockTelemetry(state.nodes, Number($('coreSelect').value || 4), isRunning());
  for (const n of state.nodes) if (t.nodeCost[n.id]) n.cost = { ...n.cost, ...t.nodeCost[n.id] };
  const rows = state.nodes.slice(0, 12).map(n => `<div class="cost-row"><b title="${n.id}">${n.id}</b><div><div class="bar-bg"><div class="bar-fill" style="width:${Math.min(100, (n.cost.cpu || 0) * 10)}%"></div></div></div><span>${(n.cost.cpu||0).toFixed(1)}%</span><span>C${n.cost.core ?? n.core ?? 0}</span></div>`).join('');
  els.costTable.innerHTML = rows;
  els.coreLoading.innerHTML = (t.cores || []).map(c => `<div class="core-row"><div class="core-row-top"><b>Core ${c.id}</b><span>${c.load.toFixed(1)}% · ${c.temperature.toFixed(1)}°C · ${(c.powerMw/1000).toFixed(2)}W</span></div><div class="bar-bg"><div class="bar-fill" style="width:${c.load}%"></div></div></div>`).join('');
  const h = t.health || {};
  els.systemHealth.innerHTML = [
    ['E2E latency', `${h.latencyMs || 0} ms`], ['Buffer', `${h.bufferOccupancy || 0}%`], ['Throughput', `${h.throughput || 0}x`], ['XRuns', h.xruns || 0], ['Memory', `${h.memoryMb || 0} MB`], ['Power', `${h.powerW || 0} W`]
  ].map(([k,v]) => `<div class="health-line"><span>${k}</span><b>${v}</b></div>`).join('');
  const inV = isRunning() ? 25 + Math.random()*65 : 3; const outV = isRunning() ? 25 + Math.random()*65 : 3;
  els.meters.innerHTML = ['IN-L','IN-R','OUT-L','OUT-R'].map((m,i) => `<div class="meter"><div class="meter-fill" style="height:${i<2?inV:outV}%"></div><div class="meter-label">${m}</div></div>`).join('');
}

async function validatePipeline() {
  const res = await api.validatePipeline(exportPipelineJson(state));
  const warnings = res?.warnings || [];
  const errors = res?.errors || [];
  els.warningStatus.textContent = `${warnings.length} WARNINGS`;
  els.errorStatus.textContent = `${errors.length} ERRORS`;
  addLog('Validate', errors.length ? 'failed' : 'success');
}

async function buildPipeline() {
  if (!isStopped()) return showToast('请先 Stop 再 Build。');
  state.runtime = 'building'; renderRunState(); addLog('Build started', state.currentPipeId);
  const res = await api.buildPipeline(exportPipelineJson(state));
  await new Promise(r => setTimeout(r, 650));
  state.sessionId = res?.session_id || `session_${Date.now()}`;
  state.runtime = 'stopped';
  state.buildStatus = res?.ok ? 'success' : 'failed';
  els.buildStatus.textContent = res?.ok ? 'BUILD SUCCESS' : 'BUILD FAILED';
  if (res?.core_map) for (const n of state.nodes) if (res.core_map[n.id] != null) n.core = res.core_map[n.id];
  addLog('Build finished', `${state.buildStatus} · ${state.sessionId}`);
  renderAll();
}

async function runPipeline() {
  if (state.runtime === 'running') return;
  if (!state.sessionId) await buildPipeline();
  await api.startRuntime(state.sessionId);
  state.runtime = 'running'; document.body.classList.remove('stopped'); document.body.classList.add('running');
  addLog('Run started', 'Pipeline locked, dynamic params enabled'); renderAll();
}

async function stopPipeline() {
  if (state.sessionId) await api.stopRuntime(state.sessionId);
  state.runtime = 'stopped'; document.body.classList.remove('running'); document.body.classList.add('stopped');
  addLog('Stopped', 'Pipeline unlocked for editing'); renderAll();
}

function renderRunState() {
  els.runBadge.className = `run-badge ${state.runtime}`;
  els.runBadge.textContent = state.runtime.toUpperCase();
}

function exportPipeline() {
  showModal(JSON.stringify(exportPipelineJson(state), null, 2));
}

function setZoom(z) { state.camera.zoom = clamp(z, 0.28, 1.65); renderCamera(); renderMiniMap(); }

function fitView(animated = false) {
  if (!state.nodes.length) return;
  const minX = Math.min(...state.nodes.map(n => n.x)); const minY = Math.min(...state.nodes.map(n => n.y));
  const maxX = Math.max(...state.nodes.map(n => n.x + getNodeSize(n).w)); const maxY = Math.max(...state.nodes.map(n => n.y + getNodeSize(n).h));
  const rect = els.viewport.getBoundingClientRect();
  const scale = clamp(Math.min((rect.width - 80) / Math.max(1, maxX - minX), (rect.height - 80) / Math.max(1, maxY - minY)), 0.35, 1.05);
  state.camera.zoom = scale;
  state.camera.x = 42 - minX * scale;
  state.camera.y = 60 - minY * scale;
  renderCamera(); renderMiniMap();
}

function renderMiniMap() {
  const cv = els.miniMap; const ctx = cv.getContext('2d'); const w = cv.width, h = cv.height;
  ctx.clearRect(0,0,w,h); ctx.fillStyle = 'rgba(5,12,24,.86)'; ctx.fillRect(0,0,w,h);
  const xs = state.nodes.map(n=>n.x), ys = state.nodes.map(n=>n.y);
  if (!xs.length) return;
  const minX = Math.min(...xs)-80, minY = Math.min(...ys)-80, maxX = Math.max(...xs)+320, maxY = Math.max(...ys)+220;
  const sx = w / Math.max(1, maxX-minX), sy = h / Math.max(1, maxY-minY); const s = Math.min(sx, sy);
  ctx.strokeStyle = 'rgba(55,217,255,.32)'; ctx.strokeRect(0.5,0.5,w-1,h-1);
  for (const e of state.edges) {
    const a=nodeById(e.from.nodeId), b=nodeById(e.to.nodeId); if(!a||!b) continue;
    ctx.strokeStyle='rgba(55,217,255,.35)'; ctx.beginPath(); ctx.moveTo((a.x-minX)*s,(a.y-minY)*s); ctx.lineTo((b.x-minX)*s,(b.y-minY)*s); ctx.stroke();
  }
  for (const n of state.nodes) { ctx.fillStyle = n.id===state.selectedNodeId ? '#ffd569' : '#37d9ff'; ctx.fillRect((n.x-minX)*s, (n.y-minY)*s, 16, 8); }
  const rect = els.viewport.getBoundingClientRect();
  const vx = (-state.camera.x/state.camera.zoom - minX)*s; const vy=(-state.camera.y/state.camera.zoom-minY)*s; const vw=rect.width/state.camera.zoom*s; const vh=rect.height/state.camera.zoom*s;
  ctx.strokeStyle='rgba(255,213,105,.88)'; ctx.strokeRect(vx,vy,vw,vh);
}

function togglePanel(id, visible) {
  state.panels[id] = visible;
  const el = $(id); if (el) el.classList.toggle('visible', visible);
}

function showModal(text) { els.modalContent.textContent = text; els.modal.classList.remove('hidden'); }
function showToast(text) { addLog('UI notice', text); }
function addLog(title, detail='') {
  state.eventLog.unshift({ time: nowTime(), title, detail }); state.eventLog = state.eventLog.slice(0, 60); renderEventLog();
}
function renderEventLog() { els.eventLog.innerHTML = state.eventLog.map(l => `<div class="log-line"><b>${l.time}</b> ${l.title}<br><span>${l.detail}</span></div>`).join(''); }

async function loop(ts = 0) {
  const running = isRunning();
  if (ts >= state.nextTelemetryAt) {
    const serverTelemetry = await api.getTelemetry(state.nodes);
    state.telemetry = serverTelemetry || makeMockTelemetry(state.nodes, Number($('coreSelect').value || 4), running);
    state.nextTelemetryAt = ts + (running ? 420 : 1600);
    renderDashboard(); renderNodes(); renderEdges(); renderInspector();
  }
  drawWaveform(els.waveCanvas, running);
  drawSpectrum(els.spectrumCanvas, running);
  requestAnimationFrame(loop);
}
