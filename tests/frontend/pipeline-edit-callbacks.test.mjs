import assert from 'node:assert/strict';
import {
  PIPELINE_TOOL_ITEMS,
  getToolItem,
  makePipelineEditPayload,
  summarizeDomPipeline,
  toolKeys,
} from '../../GUI/frontend/assets/js/pipelineEditCallbackModel.js';

assert.deepEqual(toolKeys(), ['select', 'pan', 'fit', 'undo', 'delete']);
assert.equal(getToolItem('undo').backend, false);
assert.equal(getToolItem('delete').backend, true);
assert.equal(getToolItem('missing'), null);

const fakeDoc = {
  querySelectorAll(selector) {
    if (selector === '.pipeline-world .node') {
      return [
        { classList: { contains: c => c === 'selected' } },
        { classList: { contains: () => false } },
      ];
    }
    if (selector === '.edge-layer .edge-hit') return [{}, {}, {}];
    return [];
  },
};

assert.deepEqual(summarizeDomPipeline(fakeDoc), {
  node_count: 2,
  edge_count: 3,
  selected_node_count: 1,
});

const payload = makePipelineEditPayload('connection_added', { from: 'A.out', to: 'B.in' }, fakeDoc);
assert.equal(payload.action, 'connection_added');
assert.equal(payload.detail.from, 'A.out');
assert.equal(payload.summary.edge_count, 3);
assert.ok(payload.timestamp_ms > 0);

assert.equal(PIPELINE_TOOL_ITEMS.length, 5);
console.log('pipeline-edit-callbacks.test passed');
