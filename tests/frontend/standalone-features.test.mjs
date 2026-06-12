import assert from 'node:assert/strict';
import fs from 'node:fs';

const html = fs.readFileSync(new URL('../../frontend/index.html', import.meta.url), 'utf8');
const compactHtml = html.replace(/\s+/g, '');
const compactToken = token => String(token).replace(/\s+/g, '');
const hasToken = token => html.includes(token) || compactHtml.includes(compactToken(token));
const assertToken = token => assert.ok(hasToken(token), `missing ${token}`);
const assertTokens = tokens => tokens.forEach(assertToken);

assertToken('Audio Studio');
assertToken('VeriSilicon Advanced Sound System');
assert.ok(hasToken('brand-logo"><span>AS</span>'), 'AS logo should be present');
assert.ok(!/react-app\.js/i.test(html), 'standalone UI must not load React app');

assertTokens([
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
]);

assert.ok(/e\.key\.toLowerCase\(\)\s*===\s*['"]z['"]/.test(html) && html.includes('undoEdit()'), 'missing Ctrl/Cmd+Z undo key handling');
assert.ok(/e\.key\.toLowerCase\(\)\s*===\s*['"]y['"]/.test(html) && html.includes('redoEdit()'), 'missing Ctrl/Cmd+Y redo key handling');
assertToken('id="undoBtn"');
assertToken('id="redoBtn"');

assertTokens(['buildDisconnectedLayoutGroupsV42', 'disconnected_components_pipeline_bands_v42']);
assertTokens(['copySelectionV43', 'cutSelectionV43', 'pasteSelectionV43', 'toggleNodeSelectionV43', 'toggleEdgeSelectionV43', 'collectClipboardPayloadV43']);
assert.ok(/e\.key\.toLowerCase\(\)/.test(html) && /key\s*===\s*['"]c['"]/.test(html) && html.includes('copySelectionV43()'), 'missing Ctrl/Cmd+C copy shortcut handling');
assert.ok(/key\s*===\s*['"]x['"]/.test(html) && html.includes('cutSelectionV43()'), 'missing Ctrl/Cmd+X cut shortcut handling');
assert.ok(/key\s*===\s*['"]v['"]/.test(html) && html.includes('pasteSelectionV43()'), 'missing Ctrl/Cmd+V paste shortcut handling');
assert.ok(html.includes('multiSelectKeyV43') && /ctrlKey\s*\|\|\s*e\.metaKey/.test(html), 'missing Ctrl/Cmd multi-select handling');

assertTokens(['connectedWorkingGroupsV44', 'renderWorkingPipelineSelectV44', 'focusWorkingGroupV44', 'Saved', 'Modified', 'New', 'Unsaved split', 'Unsaved merge', 'working_groups_v44', 'current connected components']);
assertTokens(['assignAlgorithmInstanceIndexesV51', 'algorithmInstanceKeyV51', 'nodeInstanceIndexRowV51', 'nodeInstanceIndexInfoV51', 'working_groups_filter_v51', 'visibleNodeIdSetV51', 'withVisibleGraphV51', 'Showing only:']);
assert.ok(/renderNodes\s*=\s*function\s*\(/.test(html) && /drawEdges\s*=\s*function\s*\(/.test(html) && /renderMinimap\s*=\s*function\s*\(/.test(html), 'pipeline group filter should filter nodes, edges and minimap');

assert.ok(hasToken('grid-column:2/4') && hasToken('grid-row:3'), 'bottom dashboard should span canvas + inspector columns only');
assert.ok(hasToken('.left-panel') && hasToken('grid-row:2/4'), 'left algorithm library should keep original full-height layout');
assert.ok(hasToken('.right-panel') && hasToken('grid-row:2;'), 'right inspector should stay in upper work area');
assertTokens(['bounded right-bottom dashboard strip', 'dashboard-docked-v47', 'dashboardCheckedOrderV47', 'onDashboardMenuChangeV47', "'logPanel'", 'Event Log']);
assert.ok(hasToken('#costPanel.hidden-panel') && hasToken('#corePanel.hidden-panel'), 'cost/core dashboard panels must have high-specificity hidden-panel CSS');
assert.ok(/style\.setProperty\(\s*['"]display['"]\s*,\s*['"]none['"]\s*,\s*['"]important['"]\s*\)/.test(html) && html.includes('audioStudioPanelVisibilityChanged'), 'setPanelVisible should force hidden display state');
assert.ok(hasToken('data-hide="leftPanel"') && hasToken('Hide Algorithm Library'), 'Algorithm Library close button should hide leftPanel');

assertTokens(['cost-table-v50', 'cost-col-core-v50', 'cost-cpu-cell-v50', 'cost-cpu-value-v50', 'sortCostTableV50', '/api/algorithm/cost/live', 'data-cost-sort', 'data-cost-center', 'cost-flash-v48', 'table-layout:fixed!important']);
assertTokens(['cost-table-v54', 'cost-col-lat-v54', 'cost-col-core-v54', 'cost-total-fixed-v56', 'cost-table-scroll-v56', 'cost-total-table-v56', 'cost-table-v59', 'cost-th-inner-v59', 'cost-sort-icon-v59', 'cost-idx-head-v59', 'cost-cpu-cell-v59', 'cost-core-cell-v59', 'cost-table-v61', 'cost-idx-cell-v61', 'idxTextForNodeV62', 'cost-idx-cell-v62', 'cleanupIdxSortHeaderV64', 'idxSortable:false']);
assert.ok(!html.includes('Backend /api/algorithm/cost/live unavailable; using frontend fake runtime cost') && !html.includes('fakeCostItemV49'), 'frontend must not synthesize fake algorithm cost data');
assert.ok(!html.includes('__audioStudioPerAlgorithmCostSortHeaderV63Installed'), 'broken sort header stabilizer must be removed');
assert.ok(!html.includes('__audioStudioPerAlgorithmCostV49BackendOnlyInstalled'), 'superseded cost V49 guard must be removed');

assertTokens(['startBufferDumpV40c', 'stopBufferDumpV40c', 'renderBufferDumpUiV40c', 'fetchBackendBufferFrameV40c', 'renderBufferInspectorV40c']);
assert.ok(!html.includes('__audioStudioBufferDumpV40bInstalled'), 'superseded buffer dump V40b guard must be removed');

assertTokens(['/api/dsp/core/loading', 'audioStudioDspCoreLoadingV65a', 'backendOwned:true', 'noFrontendFake:true', 'GET /api/dsp/core/loading failed; no frontend fake data is generated']);
assertTokens(['/api/event-log/live', '/api/system/health/live', '/api/audio/io/live', 'refreshEventLogBackendV69', 'refreshSystemHealthBackendV69', 'refreshAudioIoBackendV69', 'Backend Event Log unavailable', 'Backend System Health unavailable', 'data-hide="logPanel"', 'data-panel="logPanel"', 'backendOnly:true']);
assert.ok(/renderHealth\s*=\s*function\s*\(\s*\)\s*\{\s*renderHealthFromBackendV69\s*\(\s*\)\s*;?\s*\}\s*;?/.test(html) && /renderMeters\s*=\s*function\s*\(\s*\)\s*\{\s*renderMetersFromBackendV69\s*\(\s*\)\s*;?\s*\}\s*;?/.test(html), 'health and meters renderers must be backend-owned');

assertTokens(['/api/realtime/probe/live', '/api/realtime/probe/config', 'probeModeButton', 'probeChanAButton', 'probeChanBButton', 'probeChanAMenu', 'probeChanBMenu', 'ch0', 'fftSize:4096', 'backendOnly:true']);
assert.ok(!html.includes('probeChanASelect') && !html.includes('probeChanBSelect'), 'realtime probe should use custom channel menus');
assertTokens(['/api/target/config', 'renderDspFrequencySelectV73', 'currentDspFrequencyMHzV73', 'syncTopbarTargetToBackendV73', 'refreshTopbarTargetFromBackendV73', 'dspFrequencyMHz', 'dspFrequencyHz']);
assert.ok(hasToken('<small>Frequency</small>') || /label\.textContent\s*=\s*['"]Frequency['"]/.test(html), 'topbar RATE label should become Frequency');

assertTokens(['focusAlgorithmLibraryForNodeV45', 'ensureLibraryItemVisibleV45', 'scrollLibraryItemToMiddleV45', 'flashLibraryItemV45', 'collapsedLibraryCategories.delete', 'algorithmLibraryFocusPulseV45', 'library_focus_for_selected_node']);
assert.ok(html.includes('window.audioStudioLibraryFocusV69') && html.includes('isLibraryFocusGestureV69') && hasToken('shiftClickOnly:true') && hasToken('noRenderAutoFocus:true'), 'Algorithm Library focus should be explicit Shift-click behavior');
assert.ok(html.includes('e.shiftKey') && html.includes("scheduleLibraryFocusForSelectedNodeV45('shift_click')"), 'Pipeline node should focus Algorithm Library only on Shift-click');
assert.ok(!html.includes("scheduleLibraryFocusForSelectedNodeV45('render');"), 'ordinary render/selection should not auto-expand Algorithm Library');
assertTokens(['windowTitleColorsV66', 'applyWindowTitleColorsV66', 'panel-title-color-v66', 'panel-menu-color-v66', '--window-title-color-v66']);
assertTokens(['audio-file-removed-v67c', '.right-panel.audio-file-removed-v67c .audio-file{display:none!important;}', '.right-panel.audio-file-removed-v67c .inspector', 'id="playBtn"', 'id="fileInput"']);
assert.ok(!html.includes('__audioStudioRemoveAudioFileV67bInstalled') && !html.includes('__audioStudioRemoveAudioFileV67Installed'), 'old broken v67/v67b markers should not remain');

assert.ok(!/__audioStudio[A-Za-z0-9_]*V\d+[A-Za-z0-9_]*Installed/.test(html), 'production frontend must not keep versioned VxxInstalled markers');
assert.ok(!/if\s*\(\s*!\s*window\.__audioStudio/.test(html), 'production frontend must not keep versioned hotfix install guards');


assertTokens(['selectAllVisiblePipelineLayoutV101', 'handleSelectAllShortcutV101', 'visibleNodeIdsForSelectAllV101', 'Select all layout']);
assert.ok(/String\(e\.key\s*\|\|\s*['"]['"]\)\.toLowerCase\(\)/.test(html) && /key\s*!==\s*['"]a['"]/.test(html), 'missing Ctrl/Cmd+A visible layout select-all handling');
assertTokens(['refreshRuntimeBufferFormatsV101', '/api/runtime/buffer/formats/live', 'edgeSampleRateLabelForEdge = function', 'edge-rate-label-v101']);
assertTokens(['renderStableRuntimeParticlesV101', 'updateEdgeFlowParticlesV101', 'edge-particle-v101', 'getPointAtLength', 'stableFlowParticles:true']);



assertTokens(['syncCostTotalFooterFromRows', 'totalsFromCostRows', 'parseCostTotalCpuPercent', 'parseCostTotalMemKb', 'cost-total-cpu-dom-rows', 'cost-total-mem-dom-rows', 'dom_cost_rows']);

console.log('standalone-features.test passed');
