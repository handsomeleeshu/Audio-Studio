import assert from 'node:assert/strict';
import fs from 'node:fs';

const html = fs.readFileSync(new URL('../../frontend/index.html', import.meta.url), 'utf8');

assert.ok(html.includes('Audio Studio'), 'new top-left brand should be Audio Studio');
assert.ok(html.includes('VeriSilicon Advanced Sound System'), 'brand subtitle should be full VASS name');
assert.ok(html.includes('brand-logo"><span>AS</span>'), 'AS logo should be present');
assert.ok(!/react-app\.js/i.test(html), 'standalone UI must not load React app');

for (const token of [
  'loadPlatformConfig',
  '/api/config',
  'module_types',
  'componentFromModuleType',
  'undoEdit',
  'redoEdit',
  'pushHistory',
  'apiPost',
  '/api/pipeline/edit',
  '/api/project/save',
]) {
  assert.ok(html.includes(token), `missing ${token}`);
}

// The UI may show different human-readable shortcut labels, so test the real
// key handling code instead of requiring a brittle literal "Ctrl+Z" string.
assert.ok(
  html.includes("e.key.toLowerCase()==='z'") && html.includes('undoEdit()'),
  'missing Ctrl/Cmd+Z undo key handling'
);
assert.ok(
  html.includes("e.key.toLowerCase()==='y'") && html.includes('redoEdit()'),
  'missing Ctrl/Cmd+Y redo key handling'
);

assert.ok(html.includes('id="undoBtn"'), 'Undo button should exist');
assert.ok(html.includes('id="redoBtn"'), 'Redo button should exist');

assert.ok(
  html.includes('__audioStudioAutoArrangeV42Installed'),
  'missing v42 disconnected-component Auto Arrange override'
);
assert.ok(
  html.includes('buildDisconnectedLayoutGroupsV42'),
  'Auto Arrange should group by real graph connected components'
);
assert.ok(
  html.includes('disconnected_components_pipeline_bands_v42'),
  'Auto Arrange telemetry should report disconnected component band layout'
);
assert.ok(
  html.indexOf('__audioStudioAutoArrangeV42Installed') > html.indexOf('__audioStudioAutoArrangeV41cInstalled'),
  'v42 Auto Arrange override should be installed after v41c'
);



assert.ok(
  html.includes('__audioStudioClipboardV43Installed'),
  'missing v43 clipboard/multi-select override'
);
for (const token of [
  'copySelectionV43',
  'cutSelectionV43',
  'pasteSelectionV43',
  'toggleNodeSelectionV43',
  'toggleEdgeSelectionV43',
  'collectClipboardPayloadV43',
]) {
  assert.ok(html.includes(token), `missing ${token}`);
}
assert.ok(
  html.includes('e.key.toLowerCase()') && html.includes("key==='c'") && html.includes('copySelectionV43()'),
  'missing Ctrl/Cmd+C copy shortcut handling'
);
assert.ok(
  html.includes("key==='x'") && html.includes('cutSelectionV43()'),
  'missing Ctrl/Cmd+X cut shortcut handling'
);
assert.ok(
  html.includes("key==='v'") && html.includes('pasteSelectionV43()'),
  'missing Ctrl/Cmd+V paste shortcut handling'
);
assert.ok(
  html.includes('multiSelectKeyV43') && html.includes('ctrlKey || e.metaKey'),
  'missing Ctrl/Cmd multi-select handling'
);


assert.ok(
  html.includes('__audioStudioWorkingPipelineListV44Installed'),
  'missing v44 working pipeline group list override'
);
for (const token of [
  'connectedWorkingGroupsV44',
  'renderWorkingPipelineSelectV44',
  'focusWorkingGroupV44',
  'Saved',
  'Modified',
  'New',
  'Unsaved split',
  'Unsaved merge',
]) {
  assert.ok(html.includes(token), `missing ${token}`);
}
assert.ok(
  html.includes('working_groups_v44') && html.includes('current connected components'),
  'pipeline list should be rendered from working connected components'
);


assert.ok(
  html.includes('grid-column:2/4') && html.includes('grid-row:3'),
  'bottom dashboard should span canvas + inspector columns only'
);
assert.ok(
  html.includes('.left-panel') && html.includes('grid-row:2/4'),
  'left algorithm library should keep original full-height layout'
);
assert.ok(
  html.includes('.right-panel') && html.includes('grid-row:2;'),
  'right inspector should stay in the upper work area so dashboard can use its lower area'
);


assert.ok(
  html.includes('__audioStudioDashboardStripV47Installed'),
  'missing v47 bounded dashboard strip layout override'
);
assert.ok(
  html.includes('bounded right-bottom dashboard strip') && html.includes('dashboard-docked-v47'),
  'dashboard cards should be docked into a bounded right-bottom strip'
);
assert.ok(
  html.includes('display:flex!important') && html.includes('overflow-x:auto'),
  'bottom dashboard should be a horizontal scroll strip instead of overflowing page'
);
assert.ok(
  html.includes('dashboardCheckedOrderV47') && html.includes('onDashboardMenuChangeV47'),
  'dashboard panel order should follow user checkbox order'
);
assert.ok(
  html.includes("'logPanel'") && html.includes('Event Log'),
  'Event Log should be part of the dashboard selectable strip'
);


assert.ok(
  html.includes('__audioStudioPerAlgorithmCostV48Installed'),
  'missing v48 per-algorithm cost frontend support'
);
for (const token of [
  'cost-table-scroll-v48',
  'refreshAlgorithmCostsV48',
  '/api/algorithm/cost/live',
  'centerAndFlashCostNodeV48',
  'sortCostTableV48',
  'data-cost-sort',
  'data-cost-center',
  'cost-flash-v48'
]) {
  assert.ok(html.includes(token), `missing ${token}`);
}
assert.ok(
  html.includes('Ctrl/Cmd + click to center and flash this algorithm'),
  'missing Ctrl/Cmd name-click centering hint'
);


assert.ok(
  html.includes('__audioStudioPerAlgorithmCostV49BackendOnlyInstalled'),
  'missing v49 backend-only per-algorithm cost fix'
);
for (const token of [
  'cost-table-v49-backend-only',
  'cost-cpu-cell-v49',
  'cost-cpu-value-v49',
  'cpuColorV49',
  'refreshAlgorithmCostsV49',
  'syncTelemetryBackendOnlyV49',
  'table-layout:fixed'
]) {
  assert.ok(html.includes(token), `missing ${token}`);
}
assert.ok(
  !html.includes('Backend /api/algorithm/cost/live unavailable; using frontend fake runtime cost') && !html.includes('fakeCostItemV49'),
  'frontend must not synthesize fake algorithm cost data'
);


assert.ok(
  html.includes('__audioStudioPerAlgorithmCostV50Installed'),
  'missing v50 per-algorithm cost display fix'
);
for (const token of [
  'cost-table-v50',
  'cost-col-core-v50',
  'cost-cpu-cell-v50',
  'cost-cpu-value-v50',
  'sortCostTableV50',
  'pointerdown',
  'table-layout:fixed!important',
]) {
  assert.ok(html.includes(token), `missing ${token}`);
}
assert.ok(
  !html.includes('Backend /api/algorithm/cost/live unavailable; using frontend fake runtime cost') && !html.includes('fakeCostItemV49'),
  'frontend must not synthesize fake algorithm cost data'
);


assert.ok(
  html.includes('__audioStudioInstanceIndexAndGroupFilterV51Installed'),
  'missing v51 algorithm instance index and group filter feature'
);
for (const token of [
  'assignAlgorithmInstanceIndexesV51',
  'algorithmInstanceKeyV51',
  'nodeInstanceIndexRowV51',
  'nodeInstanceIndexInfoV51',
  'working_groups_filter_v51',
  'visibleNodeIdSetV51',
  'withVisibleGraphV51',
  'Showing only:',
]) {
  assert.ok(html.includes(token), `missing ${token}`);
}
assert.ok(
  html.includes('renderNodes=function()') && html.includes('drawEdges=function()') && html.includes('renderMinimap=function()'),
  'v51 should filter nodes, edges and minimap when a pipeline group is selected'
);

console.log('standalone-features.test passed');
