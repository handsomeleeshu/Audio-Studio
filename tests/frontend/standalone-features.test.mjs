import assert from 'node:assert/strict';
import fs from 'node:fs';

const html = fs.readFileSync(new URL('../../frontend/index.html', import.meta.url), 'utf8');

assert.ok(html.includes('Audio Studio'), 'new top-left brand should be Audio Studio');
assert.ok(html.includes('VeriSilicon Advanced Sound System'), 'brand subtitle should be full VASS name');
assert.ok(html.includes('brand-logo"><span>AS</span>'), 'AS logo should be present');
assert.ok(!/react-app\.js/i.test(html), 'standalone UI must not load React app');

for (const token of [
  'loadPlatformConfig','/api/config','module_types','componentFromModuleType',
  'undoEdit','redoEdit','pushHistory','apiPost','/api/pipeline/edit','/api/project/save',
]) assert.ok(html.includes(token), `missing ${token}`);

assert.ok(html.includes("e.key.toLowerCase()==='z'") && html.includes('undoEdit()'), 'missing Ctrl/Cmd+Z undo key handling');
assert.ok(html.includes("e.key.toLowerCase()==='y'") && html.includes('redoEdit()'), 'missing Ctrl/Cmd+Y redo key handling');
assert.ok(html.includes('id="undoBtn"'), 'Undo button should exist');
assert.ok(html.includes('id="redoBtn"'), 'Redo button should exist');

for (const token of ['buildDisconnectedLayoutGroupsV42','disconnected_components_pipeline_bands_v42']) assert.ok(html.includes(token), `missing ${token}`);
for (const token of ['copySelectionV43','cutSelectionV43','pasteSelectionV43','toggleNodeSelectionV43','toggleEdgeSelectionV43','collectClipboardPayloadV43']) assert.ok(html.includes(token), `missing ${token}`);
assert.ok(html.includes('e.key.toLowerCase()') && html.includes("key==='c'") && html.includes('copySelectionV43()'), 'missing Ctrl/Cmd+C copy shortcut handling');
assert.ok(html.includes("key==='x'") && html.includes('cutSelectionV43()'), 'missing Ctrl/Cmd+X cut shortcut handling');
assert.ok(html.includes("key==='v'") && html.includes('pasteSelectionV43()'), 'missing Ctrl/Cmd+V paste shortcut handling');
assert.ok(html.includes('multiSelectKeyV43') && html.includes('ctrlKey || e.metaKey'), 'missing Ctrl/Cmd multi-select handling');

for (const token of ['connectedWorkingGroupsV44','renderWorkingPipelineSelectV44','focusWorkingGroupV44','Saved','Modified','New','Unsaved split','Unsaved merge','working_groups_v44','current connected components']) assert.ok(html.includes(token), `missing ${token}`);
for (const token of ['assignAlgorithmInstanceIndexesV51','algorithmInstanceKeyV51','nodeInstanceIndexRowV51','nodeInstanceIndexInfoV51','working_groups_filter_v51','visibleNodeIdSetV51','withVisibleGraphV51','Showing only:']) assert.ok(html.includes(token), `missing ${token}`);
assert.ok(html.includes('renderNodes=function()') && html.includes('drawEdges=function()') && html.includes('renderMinimap=function()'), 'pipeline group filter should filter nodes, edges and minimap');

assert.ok(html.includes('grid-column:2/4') && html.includes('grid-row:3'), 'bottom dashboard should span canvas + inspector columns only');
assert.ok(html.includes('.left-panel') && html.includes('grid-row:2/4'), 'left algorithm library should keep original full-height layout');
assert.ok(html.includes('.right-panel') && html.includes('grid-row:2;'), 'right inspector should stay in upper work area');
for (const token of ['bounded right-bottom dashboard strip','dashboard-docked-v47','dashboardCheckedOrderV47','onDashboardMenuChangeV47',"'logPanel'",'Event Log']) assert.ok(html.includes(token), `missing ${token}`);
assert.ok(html.includes('#costPanel.hidden-panel') && html.includes('#corePanel.hidden-panel'), 'cost/core dashboard panels must have high-specificity hidden-panel CSS');
assert.ok(html.includes("style.setProperty('display','none','important')") && html.includes('audioStudioPanelVisibilityChanged'), 'setPanelVisible should force hidden display state');
assert.ok(html.includes('data-hide="leftPanel"') && html.includes('Hide Algorithm Library'), 'Algorithm Library close button should hide leftPanel');

for (const token of ['cost-table-v50','cost-col-core-v50','cost-cpu-cell-v50','cost-cpu-value-v50','sortCostTableV50','/api/algorithm/cost/live','data-cost-sort','data-cost-center','cost-flash-v48','table-layout:fixed!important']) assert.ok(html.includes(token), `missing ${token}`);
for (const token of ['cost-table-v54','cost-col-lat-v54','cost-col-core-v54','cost-total-fixed-v56','cost-table-scroll-v56','cost-total-table-v56','cost-table-v59','cost-th-inner-v59','cost-sort-icon-v59','cost-idx-head-v59','cost-cpu-cell-v59','cost-core-cell-v59','cost-table-v61','cost-idx-cell-v61','idxTextForNodeV62','cost-idx-cell-v62','cleanupIdxSortHeaderV64','idxSortable:false']) assert.ok(html.includes(token), `missing ${token}`);
assert.ok(!html.includes('Backend /api/algorithm/cost/live unavailable; using frontend fake runtime cost') && !html.includes('fakeCostItemV49'), 'frontend must not synthesize fake algorithm cost data');
assert.ok(!html.includes('__audioStudioPerAlgorithmCostSortHeaderV63Installed'), 'broken sort header stabilizer must be removed');
assert.ok(!html.includes('__audioStudioPerAlgorithmCostV49BackendOnlyInstalled'), 'superseded cost V49 guard must be removed');

for (const token of ['startBufferDumpV40c','stopBufferDumpV40c','renderBufferDumpUiV40c','fetchBackendBufferFrameV40c','renderBufferInspectorV40c']) assert.ok(html.includes(token), `missing ${token}`);
assert.ok(!html.includes('__audioStudioBufferDumpV40bInstalled'), 'superseded buffer dump V40b guard must be removed');

for (const token of ['/api/dsp/core/loading','audioStudioDspCoreLoadingV65a','backendOwned:true','noFrontendFake:true','GET /api/dsp/core/loading failed; no frontend fake data is generated']) assert.ok(html.includes(token), `missing ${token}`);
for (const token of ['/api/event-log/live','/api/system/health/live','/api/audio/io/live','refreshEventLogBackendV69','refreshSystemHealthBackendV69','refreshAudioIoBackendV69','Backend Event Log unavailable','Backend System Health unavailable','data-hide="logPanel"','data-panel="logPanel"','backendOnly:true']) assert.ok(html.includes(token), `missing ${token}`);
assert.ok(html.includes('renderHealth=function(){renderHealthFromBackendV69();};') && html.includes('renderMeters=function(){renderMetersFromBackendV69();};'), 'health and meters renderers must be backend-owned');

for (const token of ['/api/realtime/probe/live','/api/realtime/probe/config','probeModeButton','probeChanAButton','probeChanBButton','probeChanAMenu','probeChanBMenu','ch0','fftSize:4096','backendOnly:true']) assert.ok(html.includes(token), `missing ${token}`);
assert.ok(!html.includes('probeChanASelect') && !html.includes('probeChanBSelect'), 'realtime probe should use custom channel menus');
for (const token of ['/api/target/config','renderDspFrequencySelectV73','currentDspFrequencyMHzV73','syncTopbarTargetToBackendV73','refreshTopbarTargetFromBackendV73','dspFrequencyMHz','dspFrequencyHz']) assert.ok(html.includes(token), `missing ${token}`);
assert.ok(html.includes('<small>Frequency</small>') || html.includes("label.textContent='Frequency'"), 'topbar RATE label should become Frequency');

for (const token of ['focusAlgorithmLibraryForNodeV45','ensureLibraryItemVisibleV45','scrollLibraryItemToMiddleV45','flashLibraryItemV45','collapsedLibraryCategories.delete','algorithmLibraryFocusPulseV45','library_focus_for_selected_node']) assert.ok(html.includes(token), `missing ${token}`);
assert.ok(html.includes('window.audioStudioLibraryFocusV69') && html.includes('isLibraryFocusGestureV69') && html.includes('shiftClickOnly:true') && html.includes('noRenderAutoFocus:true'), 'Algorithm Library focus should be explicit Shift-click behavior');
assert.ok(html.includes('e.shiftKey') && html.includes("scheduleLibraryFocusForSelectedNodeV45('shift_click')"), 'Pipeline node should focus Algorithm Library only on Shift-click');
assert.ok(!html.includes("scheduleLibraryFocusForSelectedNodeV45('render');"), 'ordinary render/selection should not auto-expand Algorithm Library');
for (const token of ['windowTitleColorsV66','applyWindowTitleColorsV66','panel-title-color-v66','panel-menu-color-v66','--window-title-color-v66']) assert.ok(html.includes(token), `missing ${token}`);
for (const token of ['audio-file-removed-v67c','.right-panel.audio-file-removed-v67c .audio-file{display:none!important;}','.right-panel.audio-file-removed-v67c .inspector','id="playBtn"','id="fileInput"']) assert.ok(html.includes(token), `missing ${token}`);
assert.ok(!html.includes('__audioStudioRemoveAudioFileV67bInstalled') && !html.includes('__audioStudioRemoveAudioFileV67Installed'), 'old broken v67/v67b markers should not remain');

assert.ok(!/__audioStudio[A-Za-z0-9_]*V\d+[A-Za-z0-9_]*Installed/.test(html), 'production frontend must not keep versioned VxxInstalled markers');
assert.ok(!/if\s*\(\s*!\s*window\.__audioStudio/.test(html), 'production frontend must not keep versioned hotfix install guards');

console.log('standalone-features.test passed');
