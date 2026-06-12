import assert from 'node:assert/strict';
import fs from 'node:fs';

const html = fs.readFileSync(new URL('../../frontend/index.html', import.meta.url), 'utf8');

function sliceBetween(startToken, endToken, label) {
  const start = html.indexOf(startToken);
  assert.ok(start >= 0, `missing ${label} start token`);
  const end = html.indexOf(endToken, start + startToken.length);
  assert.ok(end > start, `missing ${label} end token`);
  return html.slice(start, end);
}

assert.ok(
  html.includes('function renderSelectionChange'),
  'selection changes should have a lightweight renderer'
);

const selectionRenderer = sliceBetween(
  'function renderSelectionChange',
  'function rectsIntersect',
  'selection renderer'
);
assert.ok(
  selectionRenderer.includes('renderInspector()') && selectionRenderer.includes('drawEdges()'),
  'selection renderer should update inspector and selected edge visuals'
);
assert.ok(
  !selectionRenderer.includes('renderCostTable()') &&
  !selectionRenderer.includes('renderCoreLoading()') &&
  !selectionRenderer.includes('renderHealth()') &&
  !selectionRenderer.includes('renderMeters()'),
  'selection renderer must not refresh dashboard panels'
);

const nodeClick = sliceBetween(
  "el.addEventListener('click'",
  "$('.delete-node', el)",
  'node click handler'
);
assert.ok(
  nodeClick.includes('renderSelectionChange') && !nodeClick.includes('renderAll(false)'),
  'node click selection should use the lightweight renderer'
);

const edgeSelect = sliceBetween(
  'function selectEdgeForEdit',
  'function drawEdges',
  'edge selection handler'
);
assert.ok(
  edgeSelect.includes('renderSelectionChange') && !edgeSelect.includes('renderAll(false)'),
  'edge selection should use the lightweight renderer'
);

const canvasBlankClick = sliceBetween(
  "wrap.addEventListener('click'",
  "$('#panelMenuBtn')",
  'canvas blank click handler'
);
assert.ok(
  canvasBlankClick.includes('renderSelectionChange') && !canvasBlankClick.includes('renderAll(false)'),
  'blank canvas deselect should use the lightweight renderer'
);

console.log('selection-render-performance.test passed');
