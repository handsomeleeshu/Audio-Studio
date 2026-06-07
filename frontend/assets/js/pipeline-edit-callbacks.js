import {
  PIPELINE_TOOL_ITEMS,
  getToolItem,
  makePipelineEditPayload,
  summarizeDomPipeline,
} from './pipelineEditCallbackModel.js';

const TOOLBOX_ID = 'audio-studio-pipeline-toolbox';
let currentTool = 'select';
let lastSummary = null;
let observerStarted = false;
let booted = false;

function postJson(path, payload) {
  console.info('[AudioStudio Frontend → Backend callback]', path, payload);
  return fetch(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  }).catch(() => ({ ok: false }));
}

function sendEdit(action, detail = {}) {
  return postJson('/api/pipeline/edit', makePipelineEditPayload(action, detail, document));
}

function sendTool(tool, extra = {}) {
  const item = getToolItem(tool);
  return postJson('/api/pipeline/tool', {
    tool,
    label: item?.label || tool,
    frontend_only: item?.backend === false,
    summary: summarizeDomPipeline(document),
    timestamp_ms: Date.now(),
    ...extra,
  });
}

function setActiveTool(key) {
  currentTool = key;
  document.querySelectorAll('.pipeline-tool').forEach(button => {
    button.classList.toggle('active', button.dataset.tool === key);
  });
  document.querySelector('.canvas-viewport')?.classList.toggle('pan-mode', key === 'pan');
}

function clickTopbarButton(text) {
  const buttons = Array.from(document.querySelectorAll('.topbar .btn'));
  const button = buttons.find(b => b.textContent.trim().toLowerCase().includes(text.toLowerCase()));
  button?.click();
  return !!button;
}

function clickZoomFit() {
  const buttons = Array.from(document.querySelectorAll('.zoom-box button'));
  const button = buttons.find(b => b.textContent.trim().toLowerCase() === 'fit');
  button?.click();
  return !!button;
}

function handleTool(key) {
  if (key === 'select' || key === 'pan') {
    setActiveTool(key);
    sendTool(key);
    return;
  }
  if (key === 'fit') {
    sendTool(key, { applied: clickZoomFit() });
    return;
  }
  if (key === 'undo') {
    window.dispatchEvent(new CustomEvent('audio-studio-undo'));
    sendTool(key, { dispatched: 'audio-studio-undo' });
    return;
  }
  if (key === 'delete') {
    sendTool(key, { forwarded_to: 'Topbar.Delete', applied: clickTopbarButton('Delete') });
  }
}

function createToolbox() {
  const box = document.createElement('div');
  box.id = TOOLBOX_ID;
  box.className = 'pipeline-legacy-toolbox';
  box.innerHTML = PIPELINE_TOOL_ITEMS.map(item => (
    `<button type="button" class="pipeline-tool ${item.key === currentTool ? 'active' : ''} ${item.key === 'pan' ? 'warn' : ''} ${item.key === 'delete' ? 'danger' : ''}" data-tool="${item.key}" title="${item.label}" aria-label="${item.label}">${item.icon}</button>`
  )).join('');

  box.addEventListener('click', event => {
    const button = event.target.closest?.('.pipeline-tool');
    if (!button) return;
    event.preventDefault();
    event.stopPropagation();
    handleTool(button.dataset.tool);
  });

  return box;
}

function updateUndoButton() {
  const button = document.querySelector('.pipeline-tool[data-tool="undo"]');
  if (!button) return;
  const ready = document.body.dataset.audioStudioCanUndo === 'true';
  button.classList.toggle('undo-ready', ready);
  button.disabled = !ready;
}

function ensureToolbox() {
  const viewport = document.querySelector('.canvas-viewport');
  if (!viewport) return false;

  let box = document.getElementById(TOOLBOX_ID);
  if (!box || box.parentElement !== viewport) {
    box?.remove();
    box = createToolbox();
    viewport.appendChild(box);
  }
  updateUndoButton();
  return true;
}

function observePipeline() {
  if (observerStarted) return;
  observerStarted = true;
  lastSummary = summarizeDomPipeline(document);

  const observer = new MutationObserver(() => {
    ensureToolbox();
    const next = summarizeDomPipeline(document);
    if (!lastSummary) {
      lastSummary = next;
      return;
    }
    if (next.node_count !== lastSummary.node_count) {
      sendEdit(next.node_count > lastSummary.node_count ? 'node_added' : 'node_removed', { before: lastSummary, after: next });
    }
    if (next.edge_count !== lastSummary.edge_count) {
      sendEdit(next.edge_count > lastSummary.edge_count ? 'connection_added' : 'connection_removed', { before: lastSummary, after: next });
    }
    lastSummary = next;
  });

  observer.observe(document.body, { childList: true, subtree: true, attributes: true, attributeFilter: ['data-audio-studio-can-undo'] });
}

function boot() {
  if (booted) return;
  if (!ensureToolbox()) {
    requestAnimationFrame(boot);
    return;
  }
  booted = true;
  observePipeline();
  sendEdit('pipeline_canvas_ready', { current_tool: currentTool });
}

boot();
