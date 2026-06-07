import { AudioStudioApi } from './api.js';
import { convertPipeline, makeNodeFromModuleType, exportPipelineJson, buildRegistry } from './configParser.js';
import { autoLayout, getPortPosition, edgePath, viewportToWorld, WORLD_WIDTH, WORLD_HEIGHT, getNodeSize } from './layout.js';
import { makeMockTelemetry, drawWaveform, drawSpectrum } from './telemetry.js';
import { groupBy, nowTime, formatNumber, safeName } from './utils.js';
import { connectUnique, validateConnection, endpointKey } from './pipelineRules.js';

const React = window.React;
const ReactDOM = window.ReactDOM;
const h = React.createElement;
const api = new AudioStudioApi('');

if (!React || !ReactDOM) {
  document.getElementById('react-load-fallback').style.display = 'block';
  document.getElementById('react-load-fallback').innerHTML = 'React 未加载。请确认网络可访问 unpkg，或执行 npm install 后按 docs 中说明改为本地构建版。';
  throw new Error('React CDN failed to load');
}

document.getElementById('react-load-fallback')?.remove();

function iconForModule(moduleTypeId = '', category = '') {
  const s = `${moduleTypeId} ${category}`.toLowerCase();
  if (s.includes('aec')) return 'AEC';
  if (s.includes('beam')) return 'BF';
  if (s.includes('agc')) return 'AGC';
  if (s.includes('drc')) return 'DRC';
  if (s.includes('eq')) return 'EQ';
  if (s.includes('mixer') || s.includes('mix')) return 'MIX';
  if (s.includes('src')) return 'SRC';
  if (s.includes('asrc')) return 'AS';
  if (s.includes('ns')) return 'NS';
  if (s.includes('volume') || s.includes('gain')) return 'VOL';
  if (s.includes('port.source')) return 'IN';
  if (s.includes('port.sink')) return 'OUT';
  if (s.includes('route')) return 'MUX';
  if (s.includes('virtual')) return '3D';
  if (s.includes('speaker')) return 'SP';
  return String(moduleTypeId).split('.').pop()?.slice(0, 3).toUpperCase() || 'ALG';
}

function App() {
  const [config, setConfig] = React.useState(null);
  const [registry, setRegistry] = React.useState(null);
  const [currentPipeId, setCurrentPipeId] = React.useState('');
  const [currentSceneId, setCurrentSceneId] = React.useState('');
  const [pipeline, setPipeline] = React.useState(null);
  const [nodes, setNodes] = React.useState([]);
  const [edges, setEdges] = React.useState([]);
  const [selectedNodeId, setSelectedNodeId] = React.useState(null);
  const [selectedEdgeId, setSelectedEdgeId] = React.useState(null);
  const [runStatus, setRunStatus] = React.useState('stopped');
  const [buildStatus, setBuildStatus] = React.useState('BUILD READY');
  const [sessionId, setSessionId] = React.useState('frontend_mock');
  const [camera, setCamera] = React.useState({ x: 0, y: 0, zoom: 1 });
  const [visiblePanels, setVisiblePanels] = React.useState(() => JSON.parse(localStorage.getItem('audioStudioPanels') || '{"library":true,"inspector":true,"dashboard":true}'));
  const [telemetry, setTelemetry] = React.useState(null);
  const [logs, setLogs] = React.useState([{ time: nowTime(), text: 'React Audio Studio loaded' }]);
  const [search, setSearch] = React.useState('');
  const [dragNode, setDragNode] = React.useState(null);
  const [connector, setConnector] = React.useState(null);
  const [modal, setModal] = React.useState(null);
  const [audioFileName, setAudioFileName] = React.useState('No file selected');
  const [dsp, setDsp] = React.useState('HiFi5s');
  const [cores, setCores] = React.useState('4');
  const [rate, setRate] = React.useState('48000');
  const viewportRef = React.useRef(null);
  const waveRef = React.useRef(null);
  const spectrumRef = React.useRef(null);
  const miniRef = React.useRef(null);

  const selectedNode = nodes.find(n => n.id === selectedNodeId) || null;
  const selectedEdge = edges.find(e => e.id === selectedEdgeId) || null;
  const projectName = config?.meta?.build?.config_name || 'A2 Project';
  const stopped = runStatus === 'stopped';

  const addLog = React.useCallback((text) => {
    setLogs(prev => [{ time: nowTime(), text }, ...prev].slice(0, 80));
  }, []);

  React.useEffect(() => {
    api.getConfig().then(cfg => {
      setConfig(cfg);
      const reg = buildRegistry(cfg);
      setRegistry(reg);
      const firstScene = cfg.scenes?.[0]?.scene_id || '';
      const firstPipe = cfg.scenes?.[0]?.active_pipelines?.[0] || cfg.pipelines?.[0]?.pipe_id || '';
      setCurrentSceneId(firstScene);
      loadPipeline(cfg, firstPipe);
      addLog(`Project loaded: ${cfg.meta?.build?.config_name || 'A2.json'}`);
    }).catch(e => addLog(`Config load failed: ${e.message}`));
  }, []);

  React.useEffect(() => {
    localStorage.setItem('audioStudioPanels', JSON.stringify(visiblePanels));
  }, [visiblePanels]);

  React.useEffect(() => {
    let timer;
    const tick = async () => {
      let data = null;
      if (nodes.length) data = await api.getTelemetry(nodes);
      if (!data) data = makeMockTelemetry(nodes, Number(cores), runStatus === 'running');
      setTelemetry(data);
      timer = setTimeout(tick, runStatus === 'running' ? 650 : 1600);
    };
    tick();
    return () => clearTimeout(timer);
  }, [nodes, runStatus, cores]);

  React.useEffect(() => {
    let raf;
    const draw = () => {
      if (waveRef.current) drawWaveform(waveRef.current, runStatus === 'running');
      if (spectrumRef.current) drawSpectrum(spectrumRef.current, runStatus === 'running');
      raf = requestAnimationFrame(draw);
    };
    draw();
    return () => cancelAnimationFrame(raf);
  }, [runStatus]);

  React.useEffect(() => { drawMiniMap(); }, [nodes, camera, visiblePanels]);

  function loadPipeline(cfg, pipeId) {
    if (!cfg || !pipeId) return;
    const converted = convertPipeline(cfg, pipeId);
    const laid = autoLayout(converted.nodes.map(n => ({ ...n })), converted.edges);
    setPipeline(converted.pipe);
    setCurrentPipeId(converted.pipe.pipe_id);
    setNodes(laid);
    setEdges(converted.edges);
    setSelectedNodeId(laid[0]?.id || null);
    setSelectedEdgeId(null);
    setCamera({ x: 0, y: 0, zoom: 1 });
  }

  function handleSceneChange(sceneId) {
    setCurrentSceneId(sceneId);
    const scene = config.scenes?.find(s => s.scene_id === sceneId);
    const pipeId = scene?.active_pipelines?.[0] || currentPipeId;
    if (pipeId) loadPipeline(config, pipeId);
    addLog(`Scene switched: ${sceneId}`);
  }

  function handlePipelineChange(pipeId) {
    loadPipeline(config, pipeId);
    addLog(`Pipeline switched: ${pipeId}`);
  }

  function handleArrange() {
    if (!stopped) return;
    const cloned = nodes.map(n => ({ ...n }));
    setNodes(autoLayout(cloned, edges));
    addLog('Auto arrange applied with minimum node distance');
  }

  async function handleValidate() {
    const payload = exportPipelineJson({ currentPipeId, pipeline, nodes, edges });
    const res = await api.validatePipeline(payload);
    addLog(res?.ok ? 'Pipeline validated' : `Validate failed: ${res?.errors?.join(', ') || 'unknown'}`);
  }

  async function handleBuild() {
    setRunStatus('building');
    setBuildStatus('BUILDING');
    const payload = exportPipelineJson({ currentPipeId, pipeline, nodes, edges });
    const res = await api.buildPipeline(payload);
    setTimeout(() => {
      setRunStatus('stopped');
      setBuildStatus(res?.ok ? 'BUILD SUCCESS' : 'BUILD FAILED');
      if (res?.session_id) setSessionId(res.session_id);
      addLog(res?.ok ? `Build success: ${res.session_id || sessionId}` : 'Build failed');
    }, 500);
  }

  async function handleRun() {
    const res = await api.startRuntime(sessionId);
    if (res?.ok) {
      setRunStatus('running');
      addLog('Runtime started');
    }
  }

  async function handleStop() {
    await api.stopRuntime(sessionId);
    setRunStatus('stopped');
    addLog('Runtime stopped; pipeline editing enabled');
  }

  function handleDelete() {
    if (!stopped) return;
    if (selectedEdgeId) {
      setEdges(prev => prev.filter(e => e.id !== selectedEdgeId));
      addLog(`Connection deleted: ${selectedEdgeId}`);
      setSelectedEdgeId(null);
      return;
    }
    if (selectedNodeId) deleteNode(selectedNodeId);
  }

  function deleteNode(nodeId) {
    if (!stopped) return;
    setNodes(prev => prev.filter(n => n.id !== nodeId));
    setEdges(prev => prev.filter(e => e.from.nodeId !== nodeId && e.to.nodeId !== nodeId));
    if (selectedNodeId === nodeId) setSelectedNodeId(null);
    addLog(`Node deleted: ${nodeId}`);
  }

  function addEdge(from, to) {
    if (!stopped) return;
    const check = validateConnection(nodes, edges, from, to);
    if (!check.ok) {
      addLog(`Connect rejected: ${check.reason}`);
      return;
    }
    setEdges(prev => connectUnique(prev, from, to));
    addLog(`Connected ${endpointKey(from)} → ${endpointKey(to)}; single in/out rule enforced`);
  }

  function updateParam(nodeId, scope, key, value) {
    setNodes(prev => prev.map(n => {
      if (n.id !== nodeId) return n;
      const field = scope === 'static' ? 'staticParams' : 'runtimeParams';
      return { ...n, [field]: { ...n[field], [key]: value } };
    }));
    if (scope === 'runtime') api.updateParam({ session_id: sessionId, node_id: nodeId, param_id: key, value });
  }

  function updateCore(nodeId, core) {
    setNodes(prev => prev.map(n => n.id === nodeId ? { ...n, core: Number(core) } : n));
  }

  function drawMiniMap() {
    const c = miniRef.current;
    const vp = viewportRef.current;
    if (!c || !vp) return;
    const ctx = c.getContext('2d');
    const w = c.width, h = c.height;
    ctx.clearRect(0, 0, w, h);
    ctx.fillStyle = 'rgba(5,12,24,.72)'; ctx.fillRect(0, 0, w, h);
    ctx.strokeStyle = 'rgba(55,217,255,.18)'; ctx.strokeRect(0.5, 0.5, w - 1, h - 1);
    const xs = nodes.map(n => n.x);
    const ys = nodes.map(n => n.y);
    if (!nodes.length) return;
    const minX = Math.min(...xs) - 180, maxX = Math.max(...xs) + 420;
    const minY = Math.min(...ys) - 160, maxY = Math.max(...ys) + 260;
    const sx = w / Math.max(1, maxX - minX);
    const sy = h / Math.max(1, maxY - minY);
    const s = Math.min(sx, sy);
    const mapX = x => (x - minX) * s + 6;
    const mapY = y => (y - minY) * s + 6;
    ctx.strokeStyle = 'rgba(55,217,255,.48)';
    for (const e of edges) {
      const a = nodes.find(n => n.id === e.from.nodeId);
      const b = nodes.find(n => n.id === e.to.nodeId);
      if (!a || !b) continue;
      ctx.beginPath(); ctx.moveTo(mapX(a.x + 218), mapY(a.y + 55)); ctx.lineTo(mapX(b.x), mapY(b.y + 55)); ctx.stroke();
    }
    for (const n of nodes) {
      ctx.fillStyle = n.id === selectedNodeId ? '#ffd569' : '#37d9ff';
      ctx.fillRect(mapX(n.x), mapY(n.y), Math.max(3, 218 * s), Math.max(3, getNodeSize(n).h * s));
    }
    const rect = vp.getBoundingClientRect();
    const vx = (-camera.x / camera.zoom);
    const vy = (-camera.y / camera.zoom);
    const vw = rect.width / camera.zoom;
    const vh = rect.height / camera.zoom;
    ctx.strokeStyle = '#42f59b';
    ctx.lineWidth = 1.5;
    ctx.strokeRect(mapX(vx), mapY(vy), vw * s, vh * s);
  }

  function onViewportPointerMove(ev) {
    const rect = viewportRef.current.getBoundingClientRect();
    const wp = viewportToWorld(ev.clientX, ev.clientY, rect, camera);
    if (dragNode && stopped) {
      setNodes(prev => prev.map(n => n.id === dragNode.nodeId ? { ...n, x: Math.max(30, wp.x - dragNode.dx), y: Math.max(30, wp.y - dragNode.dy) } : n));
    }
    if (connector) setConnector(c => c ? { ...c, current: wp } : c);
  }

  function onViewportPointerUp() {
    setDragNode(null);
    setConnector(null);
  }

  function startNodeDrag(ev, node) {
    if (!stopped) return;
    const rect = viewportRef.current.getBoundingClientRect();
    const wp = viewportToWorld(ev.clientX, ev.clientY, rect, camera);
    setDragNode({ nodeId: node.id, dx: wp.x - node.x, dy: wp.y - node.y });
  }

  function startConnector(ev, node, port) {
    if (!stopped) return;
    ev.stopPropagation();
    const pos = getPortPosition(node, 'output', port.name);
    setConnector({ from: { nodeId: node.id, portName: port.name }, start: pos, current: pos });
  }

  function finishConnector(ev, node, port) {
    if (!stopped || !connector) return;
    ev.stopPropagation();
    addEdge(connector.from, { nodeId: node.id, portName: port.name });
    setConnector(null);
  }

  function handleDrop(ev) {
    ev.preventDefault();
    if (!stopped) return;
    const typeId = ev.dataTransfer.getData('application/audio-studio-module');
    const mt = registry?.moduleTypes?.get(typeId);
    if (!mt) return;
    const rect = viewportRef.current.getBoundingClientRect();
    const wp = viewportToWorld(ev.clientX, ev.clientY, rect, camera);
    const node = makeNodeFromModuleType(mt, Math.max(30, wp.x - 100), Math.max(30, wp.y - 50));
    setNodes(prev => [...prev, node]);
    setSelectedNodeId(node.id);
    setSelectedEdgeId(null);
    addLog(`Node added: ${node.title}`);
  }

  const appClass = [
    'app-shell', runStatus,
    visiblePanels.library ? '' : 'left-hidden',
    visiblePanels.inspector ? '' : 'right-hidden',
    visiblePanels.dashboard ? '' : 'dashboard-hidden'
  ].filter(Boolean).join(' ');

  return h('div', { className: appClass },
    h(Topbar, { projectName, dsp, setDsp, cores, setCores, rate, setRate, runStatus, buildStatus, onValidate: handleValidate, onBuild: handleBuild, onRun: handleRun, onStop: handleStop, onArrange: handleArrange, onDelete: handleDelete, onExport: () => setModal(JSON.stringify(exportPipelineJson({ currentPipeId, pipeline, nodes, edges }), null, 2)), onDocs: () => setModal('docs/frontend_development.md\n\nReact 版入口：frontend/assets/js/react-app.js\n参数 UI、节点 UI、Dashboard 均已组件化。') }),
    h(AlgorithmLibrary, { visible: visiblePanels.library, toggle: () => setVisiblePanels(p => ({ ...p, library: false })), registry, search, setSearch, disabled: !stopped }),
    h('main', { className: 'pipeline-area' },
      h(PipelineHeader, { config, currentPipeId, currentSceneId, onPipelineChange: handlePipelineChange, onSceneChange: handleSceneChange, buildStatus, pipeline }),
      h('div', { className: 'canvas-viewport', ref: viewportRef, onPointerMove: onViewportPointerMove, onPointerUp: onViewportPointerUp, onDragOver: e => e.preventDefault(), onDrop: handleDrop },
        h(EdgeLayer, { nodes, edges, selectedEdgeId, setSelectedEdgeId, setSelectedNodeId, camera, running: runStatus === 'running', connector }),
        h('div', { className: 'pipeline-world', style: { transform: `translate(${camera.x}px, ${camera.y}px) scale(${camera.zoom})` } },
          nodes.map(node => h(PipelineNode, { key: node.id, node, selected: node.id === selectedNodeId, running: runStatus === 'running', telemetry: telemetry?.nodeCost?.[node.id], onSelect: () => { setSelectedNodeId(node.id); setSelectedEdgeId(null); api.nodeAction({ node_id: node.id, action: 'select' }); }, onDelete: () => deleteNode(node.id), onDragStart: startNodeDrag, onOutputDown: startConnector, onInputUp: finishConnector }))
        ),
        h('div', { className: 'canvas-hint' }, stopped ? 'Stop 状态：可添加/删除节点、拖动节点、拖拽 output → input 建立连接；点击连线后 Delete 可删除连接。' : 'Running 状态：Pipeline 结构锁定，仅允许修改动态参数。'),
        h(ZoomBox, { camera, setCamera, viewportRef, nodes }),
        h('canvas', { ref: miniRef, className: 'mini-map', width: 220, height: 130 })
      )
    ),
    h(Inspector, { visible: visiblePanels.inspector, toggle: () => setVisiblePanels(p => ({ ...p, inspector: false })), node: selectedNode, edge: selectedEdge, running: runStatus === 'running', cores: Number(cores), onParamChange: updateParam, onCoreChange: updateCore, onDeleteEdge: () => handleDelete() }),
    h(Dashboard, { visible: visiblePanels.dashboard, toggle: () => setVisiblePanels(p => ({ ...p, dashboard: false })), nodes, telemetry, logs, waveRef, spectrumRef, audioFileName, setAudioFileName, running: runStatus === 'running' }),
    h('nav', { className: 'panel-dock' },
      h('button', { onClick: () => setVisiblePanels(p => ({ ...p, library: true })) }, 'Library'),
      h('button', { onClick: () => setVisiblePanels(p => ({ ...p, inspector: true })) }, 'Inspector'),
      h('button', { onClick: () => setVisiblePanels(p => ({ ...p, dashboard: true })) }, 'Dashboard')
    ),
    modal && h('div', { className: 'modal' }, h('div', { className: 'modal-body' }, h('button', { className: 'modal-close', onClick: () => setModal(null) }, '×'), h('pre', null, modal)))
  );
}

function Topbar(p) {
  return h('header', { className: 'topbar' },
    h('div', { className: 'brand' },
      h('div', { className: 'logo-mark' }, h('div', { className: 'logo-text' }, 'AS')),
      h('div', null, h('div', { className: 'brand-title' }, 'Audio Studio'), h('div', { className: 'brand-subtitle' }, h('b', null, 'VASS'), ' · VeriSilicon Advanced Sound System'))
    ),
    h('div', { className: 'toolbar-group project' }, h('label', null, 'Project'), h('select', { value: p.projectName, onChange: () => {} }, h('option', { value: p.projectName }, p.projectName))),
    h('div', { className: 'toolbar-group' }, h('label', null, 'DSP Target'), h('select', { value: p.dsp, onChange: e => p.setDsp(e.target.value) }, ['HiFi5s','ZSP','Native Mock','DSP Simulator'].map(v => h('option', { key: v }, v)))),
    h('div', { className: 'toolbar-group small' }, h('label', null, 'Cores'), h('select', { value: p.cores, onChange: e => p.setCores(e.target.value) }, ['4','8'].map(v => h('option', { key: v }, v)))),
    h('div', { className: 'toolbar-group small' }, h('label', null, 'Rate'), h('select', { value: p.rate, onChange: e => p.setRate(e.target.value) }, ['48000','96000'].map(v => h('option', { key: v }, v)))),
    h('button', { className: 'btn blue', onClick: p.onValidate }, 'Validate'),
    h('button', { className: 'btn purple', onClick: p.onBuild, disabled: p.runStatus === 'running' }, 'Build'),
    h('button', { className: 'btn green', onClick: p.onRun, disabled: p.runStatus !== 'stopped' }, 'Run'),
    h('button', { className: 'btn red', onClick: p.onStop }, 'Stop'),
    h('button', { className: 'btn', onClick: p.onArrange, disabled: p.runStatus !== 'stopped' }, 'Auto Arrange'),
    h('button', { className: 'btn danger-outline', onClick: p.onDelete, disabled: p.runStatus !== 'stopped' }, 'Delete'),
    h('button', { className: 'btn', onClick: p.onExport }, 'Export'),
    h('button', { className: 'btn', onClick: p.onDocs }, 'Docs'),
    h('div', { className: `run-badge ${p.runStatus}` }, p.runStatus.toUpperCase())
  );
}

function PipelineHeader({ config, currentPipeId, currentSceneId, onPipelineChange, onSceneChange, buildStatus, pipeline }) {
  return h('div', { className: 'pipeline-header' },
    h('div', null, h('div', { className: 'pipeline-name' }, pipeline?.name || currentPipeId || 'Pipeline'), h('div', { className: 'pipeline-desc' }, `Loaded from project JSON · ${pipeline?.domain || '-'} · ${pipeline?.frame?.rate || 48000} Hz`)),
    h('div', { className: 'pipeline-controls' },
      h('label', null, 'Scene'), h('select', { value: currentSceneId, onChange: e => onSceneChange(e.target.value) }, (config?.scenes || []).map(s => h('option', { key: s.scene_id, value: s.scene_id }, s.scene_id))),
      h('label', null, 'Pipeline'), h('select', { value: currentPipeId, onChange: e => onPipelineChange(e.target.value) }, (config?.pipelines || []).map(p => h('option', { key: p.pipe_id, value: p.pipe_id }, p.name || p.pipe_id))),
      h('div', { className: 'status-strip' }, h('span', { className: 'status-pill success' }, buildStatus), h('span', { className: 'status-pill warn' }, '0 WARNINGS'), h('span', { className: 'status-pill error' }, '0 ERRORS'))
    )
  );
}

function AlgorithmLibrary({ visible, toggle, registry, search, setSearch, disabled }) {
  const types = Array.from(registry?.moduleTypes?.values?.() || []);
  const filtered = types.filter(mt => !search || `${mt.type_id} ${mt.category}`.toLowerCase().includes(search.toLowerCase()));
  const groups = groupBy(filtered, mt => mt.category || 'module');
  return h('aside', { className: 'left-panel panel' },
    h('div', { className: 'panel-title' }, h('span', null, 'Algorithm Library'), h('button', { className: 'panel-toggle', onClick: toggle }, '×')),
    h('input', { className: 'search', value: search, onChange: e => setSearch(e.target.value), placeholder: 'Search module type / category...' }),
    h('div', { className: 'module-library' }, Array.from(groups.entries()).map(([cat, list]) => h('div', { key: cat },
      h('div', { className: 'module-category' }, cat),
      list.map(mt => h('div', { key: mt.type_id, className: 'module-item', draggable: !disabled, onDragStart: ev => ev.dataTransfer.setData('application/audio-studio-module', mt.type_id) },
        h('div', { className: 'module-icon' }, iconForModule(mt.type_id, mt.category)),
        h('div', null, h('div', { className: 'name' }, mt.type_id), h('div', { className: 'meta' }, h('span', null, `in:${mt.io?.in_ports?.length || 0}`), h('span', null, `out:${mt.io?.out_ports?.length || 0}`), h('span', null, `abi:${mt.abi_version || '-'}`)))
      ))
    )))
  );
}

function EdgeLayer({ nodes, edges, selectedEdgeId, setSelectedEdgeId, setSelectedNodeId, camera, running, connector }) {
  const byId = new Map(nodes.map(n => [n.id, n]));
  const style = { transform: `translate(${camera.x}px, ${camera.y}px) scale(${camera.zoom})` };
  const marker = h('defs', null, h('marker', { id: 'arrow', viewBox: '0 0 8 8', refX: '7', refY: '4', markerWidth: '4', markerHeight: '4', orient: 'auto-start-reverse' }, h('path', { d: 'M 0 0 L 8 4 L 0 8 z', fill: 'rgba(55,217,255,.78)' })));
  return h('svg', { className: 'edge-layer', width: WORLD_WIDTH, height: WORLD_HEIGHT, style }, marker,
    edges.map(e => {
      const a = byId.get(e.from.nodeId), b = byId.get(e.to.nodeId);
      if (!a || !b) return null;
      const p1 = getPortPosition(a, 'output', e.from.portName);
      const p2 = getPortPosition(b, 'input', e.to.portName);
      const d = edgePath(p1, p2);
      const selected = e.id === selectedEdgeId;
      return h('g', { key: e.id },
        h('path', { className: 'edge-hit', d, onClick: ev => { ev.stopPropagation(); setSelectedEdgeId(e.id); setSelectedNodeId(null); } }),
        h('path', { className: `edge-path ${selected ? 'selected' : ''}`, d, markerEnd: 'url(#arrow)' }),
        running && h('path', { className: 'edge-flow', d })
      );
    }),
    connector && h('path', { className: 'temp-edge', d: edgePath(connector.start, connector.current) })
  );
}

function PipelineNode({ node, selected, running, telemetry, onSelect, onDelete, onDragStart, onOutputDown, onInputUp }) {
  const size = getNodeSize(node);
  return h('div', { className: `node ${selected ? 'selected' : ''} ${running ? 'running' : ''}`, style: { left: node.x, top: node.y, height: size.h }, onPointerDown: onSelect },
    h('button', { className: 'node-delete', onClick: ev => { ev.stopPropagation(); onDelete(); } }, '×'),
    h('div', { className: 'node-header', onPointerDown: ev => { onSelect(); onDragStart(ev, node); } },
      h('div', { className: 'node-title-wrap' }, h('div', { className: 'node-mini-icon' }, iconForModule(node.moduleTypeId, node.category)), h('div', { className: 'node-title' }, node.title)),
      h('div', { className: 'node-kind' }, `Core ${node.core ?? 0}`)
    ),
    h('div', { className: 'node-meta' },
      h('span', null, 'CPU ', h('b', null, `${formatNumber(telemetry?.cpu || node.cost?.cpu || 0, 1)}%`)),
      h('span', null, 'LAT ', h('b', null, `${formatNumber(telemetry?.latencyMs || 0, 2)}ms`)),
      h('span', null, 'MEM ', h('b', null, `${Math.round(telemetry?.memKb || 0)}KB`)),
      h('span', null, 'I/O ', h('b', null, `${node.inputs.length}/${node.outputs.length}`))
    ),
    h('div', { className: 'port-list inputs' }, node.inputs.map(port => h('div', { key: port.name, className: 'port input', onPointerUp: ev => onInputUp(ev, node, port), title: `${node.id}:${port.name}` }, h('span', { className: 'port-dot' }), h('span', { className: 'port-label' }, port.name)))),
    h('div', { className: 'port-list outputs' }, node.outputs.map(port => h('div', { key: port.name, className: 'port output', onPointerDown: ev => onOutputDown(ev, node, port), title: `${node.id}:${port.name}` }, h('span', { className: 'port-label' }, port.name), h('span', { className: 'port-dot' }))))
  );
}

function ZoomBox({ camera, setCamera, viewportRef, nodes }) {
  const zoom = pct => setCamera(c => ({ ...c, zoom: Math.max(.35, Math.min(1.8, +(c.zoom * pct).toFixed(2))) }));
  const fit = () => {
    const vp = viewportRef.current;
    if (!vp || !nodes.length) return;
    const rect = vp.getBoundingClientRect();
    const minX = Math.min(...nodes.map(n => n.x));
    const minY = Math.min(...nodes.map(n => n.y));
    const maxX = Math.max(...nodes.map(n => n.x + getNodeSize(n).w));
    const maxY = Math.max(...nodes.map(n => n.y + getNodeSize(n).h));
    const z = Math.max(.35, Math.min(1.2, Math.min((rect.width - 80) / (maxX - minX + 120), (rect.height - 80) / (maxY - minY + 120))));
    setCamera({ zoom: +z.toFixed(2), x: 40 - minX * z, y: 60 - minY * z });
  };
  return h('div', { className: 'zoom-box' }, h('button', { onClick: () => zoom(.85) }, '−'), h('span', null, `${Math.round(camera.zoom * 100)}%`), h('button', { onClick: () => zoom(1.15) }, '+'), h('button', { onClick: fit }, 'Fit'));
}

function Inspector({ toggle, node, edge, running, cores, onParamChange, onCoreChange, onDeleteEdge }) {
  const [tab, setTab] = React.useState('parameters');
  React.useEffect(() => setTab('parameters'), [node?.id, edge?.id]);
  let content;
  if (edge) {
    content = h('div', { className: 'inspector' }, h('div', { className: 'node-summary' }, h('div', { className: 'node-summary-title' }, 'Connection'), h('div', { className: 'node-summary-sub' }, `${endpointKey(edge.from)} → ${endpointKey(edge.to)}`)), h('button', { className: 'btn danger-outline', onClick: onDeleteEdge, disabled: running }, 'Delete Connection'));
  } else if (!node) {
    content = h('div', { className: 'inspector empty' }, 'Select a node or connection to configure.');
  } else {
    content = h('div', { className: 'inspector' },
      h('div', { className: 'node-summary' }, h('div', { className: 'node-summary-title' }, h('span', null, node.title), h('span', null, iconForModule(node.moduleTypeId, node.category))), h('div', { className: 'node-summary-sub' }, `${node.id} · ${node.moduleTypeId}`)),
      h('div', { className: 'tab-row' }, ['parameters','mapping','notes'].map(t => h('button', { key: t, className: tab === t ? 'active' : '', onClick: () => setTab(t) }, t.toUpperCase()))),
      tab === 'parameters' && h(ParamPanel, { node, running, onParamChange }),
      tab === 'mapping' && h('div', null, h('div', { className: 'param-section-title' }, 'DSP Core Mapping'), h('select', { value: node.core || 0, onChange: e => onCoreChange(node.id, e.target.value), disabled: running }, Array.from({ length: cores }, (_, i) => h('option', { key: i, value: i }, `Core ${i}`))), h('div', { className: 'param-section-title' }, 'I/O Format'), h('div', { className: 'node-summary-sub' }, `Inputs: ${node.inputs.map(p => p.name).join(', ') || '-'}\nOutputs: ${node.outputs.map(p => p.name).join(', ') || '-'}`)),
      tab === 'notes' && h('textarea', { value: JSON.stringify(node.moduleType || node.raw, null, 2), readOnly: true, style: { width: '100%', minHeight: 280 } })
    );
  }
  return h('aside', { className: 'right-panel panel' }, h('div', { className: 'panel-title' }, h('span', null, 'Inspector'), h('button', { className: 'panel-toggle', onClick: toggle }, '×')), content);
}

function ParamPanel({ node, running, onParamChange }) {
  const staticFields = node.moduleType?.static_schema?.fields || [];
  const runtimeFields = node.moduleType?.runtime_params || [];
  return h('div', null,
    h('div', { className: 'param-section-title' }, 'Static Parameters'),
    staticFields.length ? staticFields.map(field => h(ParameterControl, { key: field.key, field: { ...field, param_id: field.key, param_name: field.key, value_type: field.type }, value: node.staticParams?.[field.key], disabled: running, onChange: v => onParamChange(node.id, 'static', field.key, v) })) : h('div', { className: 'node-summary-sub' }, 'No static parameters'),
    h('div', { className: 'param-section-title' }, 'Runtime Parameters'),
    runtimeFields.length ? runtimeFields.map(field => h(ParameterControl, { key: field.param_id, field, value: node.runtimeParams?.[field.param_id], disabled: false, onChange: v => onParamChange(node.id, 'runtime', field.param_id, v) })) : h('div', { className: 'node-summary-sub' }, 'No runtime parameters')
  );
}

function ParameterControl({ field, value, disabled, onChange }) {
  const type = field.value_type || field.type;
  const name = field.param_name || field.key || field.param_id;
  const desc = [type, field.unit, field.apply?.mode].filter(Boolean).join(' · ');
  let input;
  if (type === 'bool') {
    input = h('label', { className: 'switch' }, h('input', { type: 'checkbox', checked: !!value, disabled, onChange: e => onChange(e.target.checked) }), h('span', { className: 'slider' }), h('span', null, value ? 'On' : 'Off'));
  } else if (type === 'enum' || field.enum) {
    input = h('select', { value: value ?? field.default ?? field.enum?.[0], disabled, onChange: e => onChange(e.target.value), style: { width: '100%' } }, (field.enum || field.kcontrol?.enum_labels || []).map(v => h('option', { key: v, value: v }, v)));
  } else if (field.range) {
    const min = Number(field.range.min ?? 0), max = Number(field.range.max ?? 100), step = Number(field.range.step ?? 1);
    const num = Number(value ?? field.default ?? min);
    input = h('div', { className: 'range-row' }, h('input', { type: 'range', min, max, step, value: num, disabled, onChange: e => onChange(Number(e.target.value)) }), h('input', { type: 'number', min, max, step, value: num, disabled, onChange: e => onChange(Number(e.target.value)) }));
  } else if (type === 'bytes') {
    input = h('textarea', { value: value || '', disabled, placeholder: `Binary blob placeholder, max ${field.bytes?.max_len || '-'} bytes`, onChange: e => onChange(e.target.value), style: { width: '100%', minHeight: 70 } });
  } else {
    input = h('input', { type: 'text', value: value ?? field.default ?? '', disabled, onChange: e => onChange(e.target.value), style: { width: '100%' } });
  }
  return h('div', { className: 'param-row' }, h('div', { className: 'param-head' }, h('span', null, name), h('span', { className: 'param-desc' }, desc)), input);
}

function Dashboard({ toggle, nodes, telemetry, logs, waveRef, spectrumRef, audioFileName, setAudioFileName, running }) {
  const costs = telemetry?.nodeCost || {};
  const cores = telemetry?.cores || [];
  const health = telemetry?.health || {};
  const sortedNodes = nodes.slice(0, 10);
  return h('section', { className: 'bottom-panel panel' },
    h('div', { className: 'panel-title compact' }, h('span', null, 'Runtime Dashboard'), h('button', { className: 'panel-toggle', onClick: toggle }, '×')),
    h('div', { className: 'dashboard-grid' },
      h('div', { className: 'card cost-card' }, h('div', { className: 'card-title' }, 'Per-Algorithm Cost'), h('div', { className: 'cost-table' }, sortedNodes.map(n => h('div', { key: n.id, className: 'cost-row' }, h('b', null, n.title), h('span', null, `${formatNumber(costs[n.id]?.cpu || 0, 1)}%`), h('span', null, `${Math.round(costs[n.id]?.memKb || 0)}KB`), h('span', null, `C${costs[n.id]?.core ?? n.core}`))))),
      h('div', { className: 'card core-card' }, h('div', { className: 'card-title' }, 'DSP Core Loading'), h('div', { className: 'core-loading' }, cores.map(c => h('div', { key: c.id, className: 'core-row' }, h('b', null, `Core ${c.id}`), h('div', { className: 'bar' }, h('span', { style: { width: `${Math.min(100, c.load)}%` } })), h('span', null, `${formatNumber(c.load, 0)}%`))))),
      h('div', { className: 'card wave-card' }, h('div', { className: 'card-title' }, 'Realtime Signal Probe'), h('canvas', { ref: waveRef, width: 520, height: 130 }), h('canvas', { ref: spectrumRef, width: 520, height: 90 })),
      h('div', { className: 'card health-card' }, h('div', { className: 'card-title' }, 'System Health'), h('div', { className: 'health-list' },
        row('Latency', `${formatNumber(health.latencyMs || 0, 2)} ms`), row('Buffer', `${formatNumber(health.bufferOccupancy || 0, 1)}%`), row('Throughput', `${formatNumber(health.throughput || 0, 1)}x`), row('XRuns', health.xruns || 0), row('Memory', `${formatNumber(health.memoryMb || 0, 1)} MB`), row('Power', `${formatNumber(health.powerW || 0, 2)} W`)
      )),
      h('div', { className: 'card io-card' }, h('div', { className: 'card-title' }, 'Audio I/O'), h('div', { className: 'meters' }, ['IN-L','IN-R','OUT-L','OUT-R'].map((m, i) => h('div', { key: m, title: m }, h('div', { className: 'meter' }, h('span', { style: { height: `${running ? 18 + Math.random() * 72 : 4}%` } }))))), h('div', { className: 'file-row' }, h('input', { type: 'file', accept: 'audio/*', onChange: e => setAudioFileName(e.target.files?.[0]?.name || 'No file selected') }), h('span', null, audioFileName))),
      h('div', { className: 'card log-card' }, h('div', { className: 'card-title' }, 'Event Log'), h('div', { className: 'event-log' }, logs.map((l, idx) => h('div', { key: idx, className: 'log-line' }, h('b', null, l.time), l.text))))
    )
  );
}

function row(k, v) { return h('div', { className: 'health-row', key: k }, h('span', null, k), h('b', null, v)); }

ReactDOM.createRoot(document.getElementById('root')).render(h(App));
