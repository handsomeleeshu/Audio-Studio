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



assert.ok(
  html.includes('__audioStudioPerAlgorithmCostIdxLayoutV54Installed') && html.includes('cost-table-v54'),
  'PER-ALGORITHM COST v54 should install hard IDX/LAT/Core layout override'
);
assert.ok(
  html.includes("['idx','IDX']") && html.includes('ensureCostHeaderV54'),
  'PER-ALGORITHM COST v54 should force visible IDX header after Name'
);
assert.ok(
  html.includes('cost-col-lat-v54') && html.includes('cost-col-core-v54') && html.includes('text-overflow:ellipsis'),
  'PER-ALGORITHM COST v54 should reserve LAT/Core width and ellipsize Name'
);

assert.ok(
  html.includes('__audioStudioCostTotalFixedV56Installed') && html.includes('cost-total-fixed-v56'),
  'PER-ALGORITHM COST v56 should render Total in a fixed footer outside the scroll body'
);
assert.ok(
  html.includes('idxSortable:false') && html.includes('removeAttribute(\'data-cost-sort-v50\')'),
  'PER-ALGORITHM COST IDX column must be display-only and not sortable'
);
assert.ok(
  html.includes('cost-table-scroll-v56') && html.includes('cost-total-table-v56'),
  'PER-ALGORITHM COST v56 should keep body scroll separate from fixed Total table'
);



assert.ok(
  html.includes('__audioStudioPerAlgorithmCostLayoutV59Installed'),
  'missing v59 per-algorithm cost layout post-processor'
);
for (const token of [
  'cost-table-v59',
  'cost-th-inner-v59',
  'cost-th-text-v59',
  'cost-sort-icon-v59',
  'cost-idx-head-v59',
  'cost-idx-cell-v59',
  'cost-cpu-cell-v59',
  'cost-core-cell-v59',
  'cost-total-table-v59',
]) {
  assert.ok(html.includes(token), `missing ${token}`);
}
assert.ok(
  html.includes('width:10%!important') && html.includes('width:12%!important'),
  'v60 should apply requested Per-Algorithm Cost column ratios'
);
assert.ok(
  html.includes('display:inline-flex!important') && html.includes('gap:4px!important'),
  'v59 sort arrow should be inline next to the header label'
);


assert.ok(
  html.includes("__audioStudioPerAlgorithmCostColumnRatioV60='Name 25 / IDX 10 / CPU 26 / MEM 15 / LAT 12 / Core 12'"),
  'missing v60 per-algorithm cost column ratio marker'
);
for (const token of [
  'cost-col-name-v54{width:25%!important}',
  'cost-col-idx-v54{width:10%!important}',
  'cost-col-cpu-v54{width:26%!important}',
  'cost-col-mem-v54{width:15%!important}',
  'cost-col-lat-v54{width:12%!important}',
  'cost-col-core-v54{width:12%!important}',
]) {
  assert.ok(html.includes(token), `missing v60 column ratio ${token}`);
}


assert.ok(
  html.includes('__audioStudioPerAlgorithmCostStoppedIdxV61Installed'),
  'missing v61 stopped-state IDX guard'
);
for (const token of [
  'cost-table-v61',
  'cost-idx-cell-v61',
  'applyCostStoppedIdxV61',
  'row.cells.length===5',
  '<span class="cost-num-v50">N/A</span>',
]) {
  assert.ok(html.includes(token), `missing ${token}`);
}
assert.ok(
  html.includes('width:25%!important') && html.includes('width:10%!important') && html.includes('width:26%!important') && html.includes('width:15%!important') && html.includes('width:12%!important'),
  'v61 should keep requested v60 column ratios'
);


assert.ok(
  html.includes('__audioStudioPerAlgorithmCostStoppedIdxV62Installed'),
  'missing v62 stopped-state cost IDX normalizer'
);
assert.ok(
  html.includes('idxTextForNodeV62') && html.includes("return 'N/A'"),
  'stopped per-algorithm cost IDX should render N/A instead of allowing CPU to occupy IDX'
);
assert.ok(
  html.includes('cost-idx-cell-v62') && html.includes('cost-cpu-cell-td-v62'),
  'v62 should keep IDX and CPU cells separated in stopped state'
);


assert.ok(
  html.includes('__audioStudioPerAlgorithmCostSortHeaderRecoveryV64Installed'),
  'missing v64 per-algorithm cost sort header recovery'
);
assert.ok(
  html.includes('cleanupIdxSortHeaderV64') && html.includes('idxSortable:false'),
  'IDX should remain display-only and non-sortable after v64 recovery'
);
assert.ok(
  !html.includes('__audioStudioPerAlgorithmCostSortHeaderV63Installed'),
  'broken v63 sort header stabilizer must be removed'
);

assert.ok(
  html.includes('__audioStudioDspCoreLoadingV65aInstalled'),
  'missing v65a DSP core loading frontend integration'
);
for (const token of [
  '/api/dsp/core/loading',
  'audioStudioDspCoreLoadingV65a',
  'backendOwned:true',
  'noFrontendFake:true',
  'GET /api/dsp/core/loading failed; no frontend fake data is generated',
]) {
  assert.ok(html.includes(token), `missing ${token}`);
}
assert.ok(
  html.includes('#coreRows{flex:1 1 auto!important;min-height:0!important;overflow-y:auto!important'),
  'DSP core rows must scroll independently from Total Load / Headroom'
);


assert.ok(
  html.includes('__audioStudioLibraryFocusV45Installed'),
  'missing v45 algorithm library focus feature'
);
for (const token of [
  'focusAlgorithmLibraryForNodeV45',
  'ensureLibraryItemVisibleV45',
  'scrollLibraryItemToMiddleV45',
  'flashLibraryItemV45',
  'collapsedLibraryCategories.delete',
  'algorithmLibraryFocusPulseV45',
  'library_focus_for_selected_node',
]) {
  assert.ok(html.includes(token), `missing ${token}`);
}
assert.ok(
  html.includes("$('#nodesLayer')?.addEventListener('click'") && html.includes('scheduleLibraryFocusForSelectedNodeV45'),
  'pipeline node selection should trigger Algorithm Library focus'
);


assert.ok(
  html.includes('__audioStudioWindowTitleColorsV66Installed'),
  'missing v66 colored window title/menu feature'
);
for (const token of [
  'windowTitleColorsV66',
  'applyWindowTitleColorsV66',
  'panel-title-color-v66',
  'panel-menu-color-v66',
  '--window-title-color-v66',
  'Algorithm Library',
  'Inspector',
  'Per-Algorithm Cost',
  'DSP Core Loading',
  'Signal Probe',
  'System Health',
  'Audio I/O',
  'Event Log',
]) {
  assert.ok(html.includes(token), `missing ${token}`);
}
assert.ok(
  html.includes('input[data-panel=\"${cfg.panel}\"]') && html.includes('menuLabel.style.setProperty'),
  'window checkbox list item color should be bound to matching panel title color'
);

console.log('standalone-features.test passed');
