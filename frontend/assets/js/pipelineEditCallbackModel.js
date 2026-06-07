export const PIPELINE_TOOL_ITEMS = [
  { key: 'select', icon: '⌖', label: 'Select / edit nodes', backend: false },
  { key: 'pan', icon: '✋', label: 'Pan canvas mode', backend: false },
  { key: 'fit', icon: '⌕', label: 'Fit pipeline view', backend: false },
  { key: 'arrange', icon: '↺', label: 'Auto arrange pipeline', backend: true },
  { key: 'delete', icon: '🗑', label: 'Delete selected node or connection', backend: true },
];

export function toolKeys() {
  return PIPELINE_TOOL_ITEMS.map(item => item.key);
}

export function getToolItem(key) {
  return PIPELINE_TOOL_ITEMS.find(item => item.key === key) || null;
}

export function summarizeDomPipeline(doc = document) {
  const nodes = Array.from(doc.querySelectorAll?.('.pipeline-world .node') || []);
  const edgeHits = Array.from(doc.querySelectorAll?.('.edge-layer .edge-hit') || []);
  return {
    node_count: nodes.length,
    edge_count: edgeHits.length,
    selected_node_count: nodes.filter(n => n.classList?.contains('selected')).length,
  };
}

export function makePipelineEditPayload(action, detail = {}, doc = document) {
  return {
    action,
    detail,
    summary: summarizeDomPipeline(doc),
    timestamp_ms: Date.now(),
  };
}
