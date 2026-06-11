import assert from 'node:assert/strict';
import fs from 'node:fs';

const html = fs.readFileSync(new URL('../../frontend/index.html', import.meta.url), 'utf8');

assert.ok(
  html.includes('AUDIO_STUDIO_STOPPED_ANIMATION_DELAY_MS'),
  'stopped animation loop should use an explicit low-frequency delay'
);
assert.ok(
  html.includes('wakeAnimationLoop()'),
  'running transition should wake the animation loop immediately'
);
assert.ok(
  !html.includes('if(!running){ await refreshBackendDashboardLiveV69(); renderAll(false); return; }'),
  'stopped telemetry loop must not rebuild/render the pipeline every 600 ms'
);
assert.ok(
  html.includes('dashboardPollDelayV69') && !html.includes('setInterval(refreshBackendDashboardLiveV69,900)'),
  'dashboard live refresh should use dynamic polling rather than a fixed 900 ms interval'
);
assert.ok(
  html.includes('dspCorePollDelayV65a') && !html.includes('setInterval(refreshDspCoreLoadingV65a,700)'),
  'DSP core loading refresh should use dynamic polling rather than a fixed 700 ms interval'
);
assert.ok(
  html.includes('telemetryPollDelay') && !html.includes('setInterval(updateRandom,600)'),
  'telemetry refresh should use dynamic polling rather than a fixed 600 ms interval'
);
assert.ok(
  html.includes('inspectorPollDelay') && !html.includes('setInterval(refreshInspectorBackend,350)'),
  'inspector live refresh should use dynamic polling rather than a fixed 350 ms interval'
);
assert.ok(
  html.includes('rtProbePollDelayV66Fresh') && !html.includes('setInterval(()=>rtProbeRefreshBackendV66Fresh(false),200)'),
  'realtime probe refresh should use dynamic polling rather than a fixed 200 ms interval'
);
assert.ok(
  html.includes('rtProbeShouldDrawBlankV66Fresh'),
  'realtime probe should avoid redrawing blank canvas every poll when inactive'
);
assert.ok(
  html.includes('function renderRuntimeTelemetry()'),
  'running telemetry ticks should have a lightweight runtime render path'
);
assert.ok(
  !/updateRandom[\s\S]{0,1200}renderAll\(true\)/.test(html),
  'running telemetry ticks must not rebuild pipeline node DOM/SVG layout'
);
assert.ok(
  html.includes('syncTelemetryBackendOnly,backendOnly:true'),
  'algorithm cost should expose a canonical telemetry-only path for runtime ticks'
);
assert.ok(
  html.includes('const __syncTelemetryBeforeCost=syncTelemetryFromBackend') &&
    html.includes('const ok=await syncTelemetryBackendOnly();') &&
    !html.includes('window.audioStudioAlgorithmCostV49?.syncTelemetryBackendOnlyV49'),
  'cost telemetry must use the canonical telemetry-only path without chaining through removed v49 code'
);
assert.ok(
  html.includes('EVENT_LOG_REFRESH_DEBOUNCE_MS_V69') && html.includes('scheduleEventLogRefreshV69(EVENT_LOG_RUNNING_POLL_MS_V69)'),
  'event log refreshes should be debounced and rate-limited during running telemetry ticks'
);
assert.ok(
  !html.includes('.finally(()=>refreshEventLogBackendV69())'),
  'UI event posting must not trigger an immediate full event-log refresh for every log line'
);
assert.ok(
  html.includes('applyZoom(false)'),
  'node rebuild should not redraw edges before replacing node DOM'
);
assert.ok(
  html.includes('let edgeLayoutRedrawFrame=0') && !html.includes('raf(()=>{ drawEdges(); raf(drawEdges); })'),
  'edge redraw after layout should be coalesced instead of triple-drawing every render'
);
assert.ok(
  /if\(!running\)\{\s*requestAnimationFrame\(applyCostStoppedIdxFixV62\);\s*setTimeout\(applyCostStoppedIdxFixV62,0\);/.test(html) &&
    html.includes('new MutationObserver(()=>{if(!running) requestAnimationFrame(applyCostStoppedIdxFixV62);})'),
  'stopped-state cost-table repair should not schedule extra async DOM passes while running'
);

console.log('runtime-loop-policy.test passed');
