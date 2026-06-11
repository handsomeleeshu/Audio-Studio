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

const algorithmRefresh = sliceBetween(
  'async function refreshInspectorBackend()',
  'function dbText',
  'algorithm inspector refresh'
);
assert.ok(
  algorithmRefresh.includes('inspectorSelectionStillTargetsNode'),
  'node inspector live responses should be committed only if the node is still selected'
);
assert.ok(
  /if\s*\(\s*!\s*inspectorSelectionStillTargetsNode\s*\([^)]*\)\s*\)\s*return\s+false/.test(algorithmRefresh),
  'stale node inspector responses must not overwrite a selected buffer'
);

const bufferBlock = sliceBetween(
  '// stable multi-buffer dump, no auto file-picker, no frontend fake PCM dump.',
  'function renderCostTable()',
  'v40c buffer inspector block'
);
const bufferRefresh = sliceBetween(
  'async function refreshBufferInspectorBackendV40c',
  'function renderBufferInspectorV40c',
  'buffer inspector refresh'
);
assert.ok(
  bufferRefresh.includes('const key = selectedEdgeKey'),
  'buffer refresh should capture the selected edge before awaiting backend live data'
);
assert.ok(
  /selectedEdgeKey\s*!==\s*key/.test(bufferRefresh),
  'stale buffer live responses must not overwrite a newer selected buffer'
);
assert.ok(
  !bufferRefresh.includes('renderBufferProbeV40c(ep)') && !bufferRefresh.includes('renderBufferDumpUiV40c(ep)'),
  'buffer live refresh should not rebuild probe/dump DOM on every poll'
);
assert.ok(
  bufferRefresh.includes('renderBufferInspectorV40c()') && bufferRefresh.includes('drawInspectorProbeCanvases()'),
  'buffer live refresh should reuse the stable renderer and only redraw canvases'
);

const fetchBufferProbe = sliceBetween(
  'async function fetchBufferProbeFrameV40c',
  'function updateBufferDumpWrittenUiV40c',
  'buffer probe fetch'
);
assert.ok(
  fetchBufferProbe.includes('makeBufferInspectorUnavailableStateV40c'),
  'buffer live fetch failures should produce an explicit unavailable state'
);
assert.ok(
  !fetchBufferProbe.includes('visual_fallback_only') && !fetchBufferProbe.includes('fallbackInspectorWave'),
  'buffer General data must not use frontend visual fallback/random values when backend live fetch fails'
);
assert.ok(
  !fetchBufferProbe.includes('data.format || edgeBufferFormatV40c'),
  'backend live success without a backend format must not be filled with locally inferred format'
);

const bufferApply = sliceBetween(
  'function applyInspectorBufferLiveDataToUiV40c',
  'async function refreshBufferInspectorBackendV40c',
  'buffer inspector apply'
);
assert.ok(
  /live\.source\s*===\s*['"]backend['"]/.test(bufferApply),
  'buffer General values should be treated as live only when they came from backend'
);
assert.ok(
  /if\s*\(ioIn\)\s*ioIn\.textContent\s*=\s*isLive\s*\?/.test(bufferApply),
  'buffer Format should be N/A unless backend live data is active'
);
assert.ok(
  !bufferApply.includes('const showFormat') && !bufferApply.includes('cachedEdgeRuntimeFormat(key)) ?'),
  'buffer Format must not stay visible from cached/fallback data after backend failure or stopped state'
);
assert.ok(
  !bufferApply.includes('live.format || fallbackFmt') && !bufferApply.includes('fmt.frameSamples'),
  'buffer General fields must not be filled from locally inferred format data'
);

const bufferRender = sliceBetween(
  'function renderBufferInspectorV40c()',
  'drawEdges = function',
  'buffer inspector render'
);
assert.ok(
  bufferRender.includes('BUFFER_V40c') && bufferRender.includes('inspectorRenderKey !== renderKey'),
  'buffer inspector should rebuild structural DOM only when its render key changes'
);
assert.ok(
  /renderBufferProbeV40c\s*\(ep\)[\s\S]*renderBufferDumpUiV40c\s*\(ep\)/.test(bufferRender),
  'buffer inspector should still rebuild probe/dump DOM when the structural key changes'
);

assert.ok(
  bufferBlock.includes('const __renderRuntimeTelemetryBeforeBufferV40c = renderRuntimeTelemetry'),
  'buffer inspector should wrap runtime telemetry ticks'
);
assert.ok(
  /renderRuntimeTelemetry\s*=\s*function\s*\(\s*\)\s*{[\s\S]*isInspectingBufferV40c\s*\(\s*\)[\s\S]*applyInspectorBufferLiveDataToUiV40c/.test(bufferBlock),
  'runtime telemetry ticks should preserve buffer live UI instead of applying node live UI'
);

assert.ok(
  html.includes('function scheduleViewportRedraw'),
  'viewport scroll redraw should be coalesced through requestAnimationFrame'
);
const viewportRedraw = sliceBetween(
  'function scheduleViewportRedraw',
  'function selectEdgeForEdit',
  'viewport redraw scheduler'
);
assert.ok(
  viewportRedraw.includes('renderMinimap()'),
  'scroll redraw should keep the minimap viewport in sync'
);
assert.ok(
  !viewportRedraw.includes('drawEdges()'),
  'scroll redraw should not rebuild SVG edges because edges scroll with the world'
);
assert.ok(
  html.includes("sc?.addEventListener('scroll', scheduleViewportRedraw"),
  'canvas scroll should not redraw minimap/edges directly for every scroll event'
);

console.log('buffer-inspector-live-stability.test passed');
