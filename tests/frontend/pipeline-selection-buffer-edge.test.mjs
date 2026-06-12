import fs from 'node:fs';
import assert from 'node:assert/strict';

const html = fs.readFileSync(new URL('../../frontend/index.html', import.meta.url), 'utf8');

assert.ok(html.includes('function shouldHandlePipelineSelectAllV104'), 'Ctrl+A guard should exist');
assert.ok(html.includes('function selectAllVisiblePipelineLayoutV104'), 'visible layout select-all helper should exist');
assert.ok(html.includes('selection_all_visible_layout'), 'select-all should report backend edit telemetry');
assert.ok(html.includes("document.addEventListener('keydown', e => {"), 'Ctrl+A keydown listener should be installed');
assert.ok(html.includes('selectAllVisiblePipelineLayoutV104();'), 'Ctrl+A keydown listener should invoke select-all helper');
assert.ok(html.includes("selectedNodeIds = new Set(state.nodes.map(n => n.id))"), 'Ctrl+A should select all visible nodes');
assert.ok(html.includes('selectedEdgeKeys = new Set(state.edges.map(edgeKeyForEdge))'), 'Ctrl+A should select all visible edges');

assert.ok(html.includes('function edgeParticleBeginV104'), 'stable particle begin helper should exist');
assert.ok(html.includes('appendEdgeRuntimeParticleV104(svg, key, d)'), 'drawEdges should use stable particle helper');
const runtimeBranch = html.slice(html.indexOf('const edgeRuntimeStateV104 = edgePipelineRuntimeState(ep);'), html.indexOf('function deleteSelectedEdge'));
assert.ok(runtimeBranch.includes('appendEdgeRuntimeParticleV104(svg, key, d)'), 'drawEdges runtime branch should use stable particle helper');
assert.ok(!runtimeBranch.includes('rnd(0, .7)'), 'drawEdges runtime branch should not randomize particle duration per redraw');
assert.ok(html.includes('appendEdgeSampleRateLabelV104(svg, a, b, ep, key)'), 'running edge sample-rate labels should be rendered');
const sampleRateHelper = html.slice(html.indexOf('function edgeSampleRateLabelForEdge'), html.indexOf('function setupPanelMenuAutoClose'));
assert.ok(sampleRateHelper.includes("if (!fmt || fmt.source !== 'backend') return 'N/A';"), 'sample-rate label should show N/A when backend format is unavailable');
assert.ok(sampleRateHelper.includes("return Number.isFinite(rate) && rate > 0 ? formatSampleRateLabel(rate) : 'N/A';"), 'sample-rate label should show N/A when backend rate is invalid');
assert.ok(!sampleRateHelper.includes('inferredEdgeFormat(ep)'), 'sample-rate label must not fall back to inferred frontend format');
assert.ok(!sampleRateHelper.includes('48000'), 'sample-rate label helper must not default to synthetic 48 kHz');
assert.ok(html.includes('normalized.backendSampleRate = backendSampleRate'), 'backend sample-rate should be recorded only from raw backend API fields');
assert.ok(/const edgeRunningV104 = edgeRuntimeStateV104 === 'running'/.test(html), 'edge particles should follow pipeline-scoped runtime state');

console.log('pipeline-selection-buffer-edge.test passed');
