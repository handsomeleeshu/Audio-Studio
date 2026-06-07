import { AudioStudioApi } from './api.js';
import { convertPipeline, makeNodeFromModuleType, exportPipelineJson, buildRegistry } from './configParser.js';
import { autoLayout, getPortPosition, edgePath, viewportToWorld, WORLD_WIDTH, WORLD_HEIGHT, getNodeSize } from './layout.js';
import { makeMockTelemetry, drawWaveform, drawSpectrum } from './telemetry.js';
import { groupBy, nowTime, formatNumber } from './utils.js';
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
  if (s.includes('file') || s.includes('port.source')) return '📄';
  if (s.includes('mic')) return '🎙';
  if (s.includes('sink') || s.includes('output') || s.includes('speaker')) return '🔊';
  if (s.includes('aec')) return '○';
  if (s.includes('beam')) return '⌁';
  if (s.includes('noise') || s.includes('ns')) return '≋';
  if (s.includes('agc')) return '↕';
  if (s.includes('vad')) return '⏻';
  if (s.includes('kws')) return '◎';
  if (s.includes('eq')) return '≋';
  if (s.includes('drc')) return '▰';
  if (s.includes('mixer') || s.includes('mix')) return '▦';
  if (s.includes('volume') || s.includes('gain')) return '◀';
  if (s.includes('asrc')) return '⟲';
  if (s.includes('src')) return '⟳';
  if (s.includes('delay')) return '⏱';
  if (s.includes('route')) return '⇄';
  if (s.includes('virtual')) return '✦';
  if (s.includes('neural') || s.includes('ai')) return '✹';
  return '≋';
}

function moduleFriendlyName(moduleTypeId = '', category = '') {
  const raw = String(moduleTypeId || '');
  const s = `${raw} ${category}`.toLowerCase();
  if (s.includes('file') || s.includes('port.source')) return 'File Input';
  if (s.includes('mic')) return 'Mic Input';
  if (s.includes('sink') || s.includes('output') || s.includes('speaker')) return 'Audio Output';
  if (s.includes('aec')) return 'AEC';
  if (s.includes('beam')) return 'Beamformer';
  if (s.includes('noise') || s.includes('ns')) return 'Noise Suppression';
  if (s.includes('agc')) return 'AGC';
  if (s.includes('vad')) return 'VAD';
  if (s.includes('kws')) return 'KWS';
  if (s.includes('eq')) return 'EQ';
  if (s.includes('drc')) return 'DRC';
  if (s.includes('mixer') || s.includes('mix')) return 'Mixer';
  if (s.includes('volume') || s.includes('gain')) return 'Volume';
  if (s.includes('asrc')) return 'ASRC';
  if (s.includes('src')) return 'SRC';
  if (s.includes('delay')) return 'Delay';
  if (s.includes('neural')) return 'Neural Denoiser';
  return raw.split('.').pop()?.replace(/[_-]+/g, ' ').replace(/\b\w/g, c => c.toUpperCase()) || 'Algorithm';
}

function moduleSummary(mt = {}) {
  const s = `${mt.type_id || ''} ${mt.category || ''}`.toLowerCase();
  const inPorts = mt.io?.in_ports?.length || 0;
  const outPorts = mt.io?.out_ports?.length || 0;
  const channels = s.includes('mic') || s.includes('aec') || s.includes('beam') ? '4ch'
    : s.includes('kws') || s.includes('vad') || s.includes('agc') || s.includes('noise') || s.includes('ns') || s.includes('neural') ? '1ch'
    : inPorts || outPorts ? `${Math.max(inPorts, outPorts)}ch`
    : 'N';
  const cpu = s.includes('neural') ? '10.5' : s.includes('beam') ? '7.8' : s.includes('aec') ? '6.1'
    : s.includes('noise') || s.includes('ns') ? '5.4' : s.includes('kws') ? '3.2' : s.includes('agc') ? '2.3'
    : s.includes('asrc') ? '2.1' : s.includes('drc') ? '1.5' : s.includes('eq') ? '1.1'
    : s.includes('vad') ? '0.9' : s.includes('src') || s.includes('mix') ? '0.8'
    : s.includes('output') || s.includes('sink') || s.includes('delay') ? '0.3' : '0.2';
  return `${channels} · ~${cpu}% CPU`;
}

function cloneNode(n) {
  return {
    ...n,
    raw: n.raw ? JSON.parse(JSON.stringify(n.raw)) : n.raw,
    moduleType: n.moduleType ? JSON.parse(JSON.stringify(n.moduleType)) : n.moduleType,
    inputs: (n.inputs || []).map(p => ({ ...p })),
    outputs: (n.outputs || []).map(p => ({ ...p })),
    staticParams: { ...(n.staticParams || {}) },
    runtimeParams: { ...(n.runtimeParams || {}) },
    cost: { ...(n.cost || {}) }
  };
}

function cloneNodes(nodes) { return (nodes || []).map(cloneNode); }
function cloneEdges(edges) { return (edges || []).map(e => ({ ...e, from: { ...e.from }, to: { ...e.to } })); }

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
  const [undoVersion, setUndoVersion] = React.useState(0);

  const viewportRef = React.useRef(null);
  const waveRef = React.useRef(null);
  const spectrumRef = React.useRef(null);
  const miniRef = React.useRef(null);
  const nodesRef = React.useRef([]);
  const edgesRef = React.useRef([]);
  const selectedNodeRef = React.useRef(null);
  const selectedEdgeRef = React.useRef(null);
  const cameraRef = React.useRef(camera);
  const undoStackRef = React.useRef([]);
  const dragSessionRef = React.useRef(null);
  const connectorRef = React.useRef(null);

  const selectedNode = nodes.find(n => n.id === selectedNodeId) || null;
  const selectedEdge = edges.find(e => e.id === selectedEdgeId) || null;
  const projectName = config?.meta?.build?.config_name || 'A2 Project';
  const stopped = runStatus === 'stopped';

  React.useEffect(() => { nodesRef.current = nodes; }, [nodes]);
  React.useEffect(() => { edgesRef.current = edges; }, [edges]);
  React.useEffect(() => { selectedNodeRef.current = selectedNodeId; }, [selectedNodeId]);
  React.useEffect(() => { selectedEdgeRef.current = selectedEdgeId; }, [selectedEdgeId]);
  React.useEffect(() => { cameraRef.current = camera; }, [camera]);

  React.useEffect(() => {
    document.body.dataset.audioStudioCanUndo = undoStackRef.current.length ? 'true' : 'false';
  }, [undoVersion]);

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

  React.useEffect(() => { drawMiniMap(); }, [nodes, edges, camera, visiblePanels]);

  React.useEffect(() => {
    const onUndo = () => handleUndo();
    const onKey = ev => {
      const target = ev.target;
      const tag = target?.tagName?.toLowerCase?.();
      if (tag === 'input' || tag === 'textarea' || tag === 'select') return;
      if ((ev.metaKey || ev.ctrlKey) && ev.key.toLowerCase() === 'z') {
        ev.preventDefault();
        handleUndo();
      }
    };
    window.addEventListener('audio-studio-undo', onUndo);
    window.addEventListener('keydown', onKey);
    return () => {
      window.removeEventListener('audio-studio-undo', onUndo);
      window.removeEventListener('keydown', onKey);
    };
  });

  function pushUndo(label) {
    if (!stopped) return;
    const snap = {
      label,
      nodes: cloneNodes(nodesRef.current),
      edges: cloneEdges(edgesRef.current),
      selectedNodeId: selectedNodeRef.current,
      selectedEdgeId: selectedEdgeRef.current,
      time: Date.now()
    };
    undoStackRef.current = [...undoStackRef.current, snap].slice(-80);
    setUndoVersion(v => v + 1);
  }

  function clearUndo() {
    undoStackRef.current = [];
    setUndoVersion(v => v + 1);
  }

  function handleUndo() {
    if (!stopped) {
      addLog('Undo ignored: runtime is not stopped');
      return;
    }
    const snap = undoStackRef.current.pop();
    if (!snap) {
      addLog('Nothing to undo');
      setUndoVersion(v => v + 1);
      return;
    }
    setNodes(cloneNodes(snap.nodes));
    setEdges(cloneEdges(snap.edges));
    setSelectedNodeId(snap.selectedNodeId || null);
    setSelectedEdgeId(snap.selectedEdgeId || null);
    addLog(`Undo: ${snap.label}`);
    api.pipelineEdit({ action: 'undo', label: snap.label, nodes: snap.nodes.length, edges: snap.edges.length });
    setUndoVersion(v => v + 1);
  }

  function notifyEdit(action, detail = {}) {
    api.pipelineEdit({
      action,
      detail,
      nodes: nodesRef.current.length,
      edges: edgesRef.current.length,
      pipe_id: currentPipeId,
      timestamp_ms: Date.now()
    });
  }

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
    clearUndo();
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
    pushUndo('Auto arrange');
    const cloned = cloneNodes(nodesRef.current);
    const arranged = autoLayout(cloned, edgesRef.current);
    setNodes(arranged);
    addLog('Auto arrange applied with minimum node distance');
    notifyEdit('auto_arrange');
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
      pushUndo('Delete connection');
      const id = selectedEdgeId;
      setEdges(prev => prev.filter(e => e.id !== id));
      addLog(`Connection deleted: ${id}`);
      setSelectedEdgeId(null);
      notifyEdit('connection_removed', { edge_id: id });
      return;
    }
    if (selectedNodeId) deleteNode(selectedNodeId, true);
  }

  function deleteNode(nodeId, withUndo = true) {
    if (!stopped) return;
    if (withUndo) pushUndo('Delete node');
    setNodes(prev => prev.filter(n => n.id !== nodeId));
    setEdges(prev => prev.filter(e => e.from.nodeId !== nodeId && e.to.nodeId !== nodeId));
    if (selectedNodeId === nodeId) setSelectedNodeId(null);
    addLog(`Node deleted: ${nodeId}`);
    notifyEdit('node_removed', { node_id: nodeId });
  }

  function addEdge(from, to) {
    if (!stopped) return false;
    const check = validateConnection(nodesRef.current, edgesRef.current, from, to);
    if (!check.ok) {
      addLog(`Connect rejected: ${check.reason}`);
      return false;
    }
    pushUndo('Add connection');
    let added = false;
    setEdges(prev => {
      const next = connectUnique(prev, from, to);
      added = next !== prev && JSON.stringify(next) !== JSON.stringify(prev);
      return next;
    });
    addLog(`Connected ${endpointKey(from)} → ${endpointKey(to)}; single in/out rule enforced`);
    notifyEdit('connection_added', { from, to });
    return true;
  }

  function updateParam(nodeId, scope, key, value) {
    if (scope === 'static' && stopped) pushUndo(`Update static parameter ${key}`);
    setNodes(prev => prev.map(n => {
      if (n.id !== nodeId) return n;
      const field = scope === 'static' ? 'staticParams' : 'runtimeParams';
      return { ...n, [field]: { ...n[field], [key]: value } };
    }));
    if (scope === 'runtime') api.updateParam({ session_id: sessionId, node_id: nodeId, param_id: key, value });
  }

  function updateCore(nodeId, core) {
    if (stopped) pushUndo('Update core mapping');
    setNodes(prev => prev.map(n => n.id === nodeId ? { ...n, core: Number(core) } : n));
    notifyEdit('core_mapping_updated', { node_id: nodeId, core: Number(core) });
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
      const as = getNodeSize(a);
      const bs = getNodeSize(b);
      ctx.beginPath();
      ctx.moveTo(mapX(a.x + as.w), mapY(a.y + as.h / 2));
      ctx.lineTo(mapX(b.x), mapY(b.y + bs.h / 2));
      ctx.stroke();
    }
    for (const n of nodes) {
      const ns = getNodeSize(n);
      ctx.fillStyle = n.id === selectedNodeId ? '#ffd569' : '#37d9ff';
      ctx.fillRect(mapX(n.x), mapY(n.y), Math.max(3, ns.w * s), Math.max(3, ns.h * s));
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
    const rect = viewportRef.current?.getBoundingClientRect();
    if (!rect) return;
    const wp = viewportToWorld(ev.clientX, ev.clientY, rect, cameraRef.current);
    if (connectorRef.current) setConnector(c => c ? { ...c, current: wp } : c);
  }

  function onViewportPointerUp() {
    setConnector(null);
    connectorRef.current = null;
  }

  function startNodeDrag(ev, node) {
    if (!stopped) return;
    ev.preventDefault();
    ev.stopPropagation();
    const rect = viewportRef.current.getBoundingClientRect();
    const wp = viewportToWorld(ev.clientX, ev.clientY, rect, cameraRef.current);
    let moved = false;
    let raf = 0;
    let last = null;
    setSelectedNodeId(node.id);
    setSelectedEdgeId(null);
    dragSessionRef.current = { nodeId: node.id };
    setDragNode({ nodeId: node.id });

    const applyMove = () => {
      raf = 0;
      if (!last) return;
      const nextX = Math.max(30, last.x - (wp.x - node.x));
      const nextY = Math.max(30, last.y - (wp.y - node.y));
      setNodes(prev => prev.map(n => n.id === node.id ? { ...n, x: nextX, y: nextY } : n));
    };

    const move = e => {
      if (e.pointerId !== ev.pointerId) return;
      const r = viewportRef.current?.getBoundingClientRect();
      if (!r) return;
      const p = viewportToWorld(e.clientX, e.clientY, r, cameraRef.current);
      const distance = Math.hypot(p.x - wp.x, p.y - wp.y);
      if (!moved && distance > 1.5) {
        pushUndo('Move node');
        moved = true;
      }
      last = p;
      if (!raf) raf = requestAnimationFrame(applyMove);
    };

    const up = e => {
      if (e.pointerId !== ev.pointerId) return;
      document.removeEventListener('pointermove', move, true);
      document.removeEventListener('pointerup', up, true);
      if (raf) cancelAnimationFrame(raf);
      if (last) applyMove();
      setDragNode(null);
      dragSessionRef.current = null;
      if (moved) {
        addLog(`Node moved: ${node.id}`);
        notifyEdit('node_moved', { node_id: node.id });
      }
    };

    document.addEventListener('pointermove', move, true);
    document.addEventListener('pointerup', up, true);
  }

  function startConnector(ev, node, port) {
    if (!stopped) return;
    ev.preventDefault();
    ev.stopPropagation();
    const start = getPortPosition(node, 'output', port.name);
    const initial = { from: { nodeId: node.id, portName: port.name }, start, current: start };
    setConnector(initial);
    connectorRef.current = initial;
    document.querySelector('.canvas-viewport')?.classList.add('connecting');

    const move = e => {
      if (e.pointerId !== ev.pointerId) return;
      const rect = viewportRef.current?.getBoundingClientRect();
      if (!rect) return;
      const current = viewportToWorld(e.clientX, e.clientY, rect, cameraRef.current);
      connectorRef.current = { ...connectorRef.current, current };
      setConnector(c => c ? { ...c, current } : c);
    };

    const up = e => {
      if (e.pointerId !== ev.pointerId) return;
      document.removeEventListener('pointermove', move, true);
      document.removeEventListener('pointerup', up, true);
      document.querySelector('.canvas-viewport')?.classList.remove('connecting');

      const el = document.elementFromPoint(e.clientX, e.clientY)?.closest?.('.port.input');
      const targetNodeId = el?.dataset?.nodeId;
      const targetPortName = el?.dataset?.portName;
      if (targetNodeId && targetPortName && targetNodeId !== node.id) {
        addEdge({ nodeId: node.id, portName: port.name }, { nodeId: targetNodeId, portName: targetPortName });
      }
      setConnector(null);
      connectorRef.current = null;
    };

    document.addEventListener('pointermove', move, true);
    document.addEventListener('pointerup', up, true);
  }

  function finishConnector(ev, node, port) {
    if (!stopped || !connectorRef.current) return;
    ev.preventDefault();
    ev.stopPropagation();
    const from = connectorRef.current.from;
    if (from.nodeId !== node.id) addEdge(from, { nodeId: node.id, portName: port.name });
    setConnector(null);
    connectorRef.current = null;
  }

  function handleDrop(ev) {
    ev.preventDefault();
    if (!stopped) return;
    const typeId = ev.dataTransfer.getData('application/audio-studio-module');
    const mt = registry?.moduleTypes?.get(typeId);
    if (!mt) return;
    const rect = viewportRef.current.getBoundingClientRect();
    const wp = viewportToWorld(ev.clientX, ev.clientY, rect, camera);
    const node = makeNodeFromModuleType(mt, Math.max(30, wp.x - 64), Math.max(30, wp.y - 58));
    pushUndo('Add node');
    setNodes(prev => [...prev, node]);
    setSelectedNodeId(node.id);
    setSelectedEdgeId(null);
    addLog(`Node added: ${node.title}`);
    notifyEdit('node_added', { node_id: node.id, module_type: node.moduleTypeId });
  }

  const appClass = [
    'app-shell', runStatus,
    visiblePanels.library ? '' : 'left-hidden',
    visiblePanels.inspector ? '' : 'right-hidden',
    visiblePanels.dashboard ? '' : 'dashboard-hidden'
  ].filter(Boolean).join(' ');

  return h('div', { className: appClass },
    h(Topbar, { projectName, dsp, setDsp, cores, setCores, rate, setRate, runStatus, buildStatus, canUndo: undoStackRef.current.length > 0, onUndo: handleUndo, onValidate: handleValidate, onBuild: handleBuild, onRun: handleRun, onStop: handleStop, onArrange: handleArrange, onDelete: handleDelete, onExport: () => setModal(JSON.stringify(exportPipelineJson({ currentPipeId, pipeline, nodes, edges }), null, 2)), onDocs: () => setModal('docs/frontend_development.md\\n\\nReact 版入口：frontend/assets/js/react-app.js\\n参数 UI、节点 UI、Dashboard 均已组件化。') }),
    h(AlgorithmLibrary, { visible: visiblePanels.library, toggle: () => setVisiblePanels(p => ({ ...p, library: false })), registry, search, setSearch, disabled: !stopped }),
    h('main', { className: 'pipeline-area' },
      h(PipelineHeader, { config, currentPipeId, currentSceneId, onPipelineChange: handlePipelineChange, onSceneChange: handleSceneChange, buildStatus, pipeline }),
      h('div', { className: 'canvas-viewport', ref: viewportRef, onPointerMove: onViewportPointerMove, onPointerUp: onViewportPointerUp, onDragOver: e => e.preventDefault(), onDrop: handleDrop },
        h(EdgeLayer, { nodes, edges, selectedEdgeId, setSelectedEdgeId, setSelectedNodeId, camera, running: runStatus === 'running', connector }),
        h('div', { className: 'pipeline-world', style: { transform: `translate(${camera.x}px, ${camera.y}px) scale(${camera.zoom})` } },
          nodes.map(node => h(PipelineNode, { key: node.id, node, selected: node.id === selectedNodeId, dragging: dragNode?.nodeId === node.id, running: runStatus === 'running', telemetry: telemetry?.nodeCost?.[node.id], onSelect: () => { setSelectedNodeId(node.id); setSelectedEdgeId(null); api.nodeAction({ node_id: node.id, action: 'select' }); }, onDelete: () => deleteNode(node.id), onDragStart: startNodeDrag, onOutputDown: startConnector, onInputUp: finishConnector }))
        ),
        h('div', { className: 'canvas-hint' }, stopped ? 'Stop 状态：可添加/删除节点、拖动节点、拖拽 output → input 建立连接；⌘Z/Ctrl+Z 或左上角 ↺ 可撤销上一步。' : 'Running 状态：Pipeline 结构锁定，仅允许修改动态参数。'),
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
      h('div', { className: 'brand-copy' }, h('div', { className: 'brand-title' }, 'Audio Studio'), h('div', { className: 'brand-subtitle' }, 'VeriSilicon Advanced Sound System'))
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
    h('button', { className: 'btn', onClick: p.onUndo, disabled: p.runStatus !== 'stopped' || !p.canUndo }, 'Undo'),
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
      list.map(mt => {
        const displayName = moduleFriendlyName(mt.type_id, mt.category);
        return h('div', { key: mt.type_id, className: 'module-item', draggable: !disabled, onDragStart: ev => ev.dataTransfer.setData('application/audio-studio-module', mt.type_id) },
          h('div', { className: 'module-icon' }, iconForModule(mt.type_id, mt.category)),
          h('div', { className: 'module-name-block' }, h('div', { className: 'name' }, displayName)),
          h('div', { className: 'module-summary' }, moduleSummary(mt))
        );
      })
    )))
  );
}

function EdgeLayer({ nodes, edges, selectedEdgeId, setSelectedEdgeId, setSelectedNodeId, camera, running, connector }) {
  const byId = new Map(nodes.map(n => [n.id, n]));
  const style = { transform: `translate(${camera.x}px, ${camera.y}px) scale(${camera.zoom})` };
  const defs = h('defs', null,
    h('linearGradient', { id: 'edgeGrad', x1: '0%', y1: '0%', x2: '100%', y2: '0%' },
      h('stop', { offset: '0%', stopColor: '#00d8ff' }),
      h('stop', { offset: '100%', stopColor: '#b65cff' })
    ),
    h('marker', { id: 'arrow', viewBox: '0 0 8 8', refX: '7', refY: '4', markerWidth: '4', markerHeight: '4', orient: 'auto-start-reverse' },
      h('path', { d: 'M 0 0 L 8 4 L 0 8 z', fill: '#00d8ff' })
    )
  );
  return h('svg', { className: 'edge-layer', width: WORLD_WIDTH, height: WORLD_HEIGHT, style }, defs,
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

function PipelineNode({ node, selected, dragging, running, telemetry, onSelect, onDelete, onDragStart, onOutputDown, onInputUp }) {
  const size = getNodeSize(node);
  return h('div', { className: `node ${selected ? 'selected' : ''} ${dragging ? 'dragging' : ''} ${running ? 'running' : ''}`, style: { left: node.x, top: node.y, height: size.h }, onPointerDown: ev => { ev.stopPropagation(); onSelect(); } },
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
    h('div', { className: 'port-list inputs' }, node.inputs.map(port => h('div', { key: port.name, className: 'port input', 'data-node-id': node.id, 'data-port-name': port.name, onPointerUp: ev => onInputUp(ev, node, port), title: `${node.id}:${port.name}` }, h('span', { className: 'port-dot' }), h('span', { className: 'port-label' }, port.name)))),
    h('div', { className: 'port-list outputs' }, node.outputs.map(port => h('div', { key: port.name, className: 'port output', 'data-node-id': node.id, 'data-port-name': port.name, onPointerDown: ev => onOutputDown(ev, node, port), title: `${node.id}:${port.name}` }, h('span', { className: 'port-label' }, port.name), h('span', { className: 'port-dot' }))))
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
      tab === 'mapping' && h('div', null, h('div', { className: 'param-section-title' }, 'DSP Core Mapping'), h('select', { value: node.core || 0, onChange: e => onCoreChange(node.id, e.target.value), disabled: running }, Array.from({ length: cores }, (_, i) => h('option', { key: i, value: i }, `Core ${i}`))), h('div', { className: 'param-section-title' }, 'I/O Format'), h('div', { className: 'node-summary-sub' }, `Inputs: ${node.inputs.map(p => p.name).join(', ') || '-'}\\nOutputs: ${node.outputs.map(p => p.name).join(', ') || '-'}`)),
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
