import { strict as assert } from 'assert';
import fs from 'fs';

const html = fs.readFileSync(new URL('../../GUI/frontend/index.html', import.meta.url), 'utf8');
const compactHtml = html.replace(/\s+/g, '');
const compactToken = token => String(token).replace(/\s+/g, '');
const hasToken = token => html.includes(token) || compactHtml.includes(compactToken(token));

assert.ok(hasToken('AUDIO_STUDIO_STOPPED_ANIMATION_DELAY_MS'), 'stopped animation loop should use an explicit low-frequency delay');
assert.ok(hasToken('wakeAnimationLoop()'), 'running transition should wake the animation loop immediately');
assert.ok(!compactHtml.includes(compactToken('if(!running){ await refreshBackendDashboardLiveV69(); renderAll(false); return; }')), 'stopped telemetry loop must not rebuild/render the pipeline every 600 ms');
assert.ok(hasToken('dashboardPollDelayV69') && !hasToken('setInterval(refreshBackendDashboardLiveV69,900)'), 'dashboard live refresh should use dynamic polling rather than a fixed 900 ms interval');
assert.ok(hasToken('dspCorePollDelayV65a') && !hasToken('setInterval(refreshDspCoreLoadingV65a,700)'), 'DSP core loading refresh should use dynamic polling rather than a fixed 700 ms interval');
assert.ok(hasToken('telemetryPollDelay') && !hasToken('setInterval(updateRandom,600)'), 'telemetry refresh should use dynamic polling rather than a fixed 600 ms interval');
assert.ok(hasToken('inspectorPollDelay') && !hasToken('setInterval(refreshInspectorBackend,350)'), 'inspector live refresh should use dynamic polling rather than a fixed 350 ms interval');
assert.ok(hasToken('rtProbePollDelayV66Fresh') && !hasToken('setInterval(()=>rtProbeRefreshBackendV66Fresh(false),200)'), 'realtime probe refresh should use dynamic polling rather than a fixed 200 ms interval');
assert.ok(hasToken('rtProbeShouldDrawBlankV66Fresh'), 'realtime probe should avoid redrawing blank canvas every poll when inactive');
assert.ok(/function\s+renderRuntimeTelemetry\s*\(/.test(html), 'running telemetry ticks should have a lightweight runtime render path');
assert.ok(!/updateRandom[\s\S]{0,1600}renderAll\s*\(\s*true\s*\)/.test(html), 'running telemetry ticks must not rebuild pipeline node DOM/SVG layout');
assert.ok(hasToken('syncTelemetryBackendOnly,backendOnly:true'), 'algorithm cost should expose a canonical telemetry-only path for runtime ticks');
assert.ok(
  /const\s+__syncTelemetryBeforeCost\s*=\s*syncTelemetryFromBackend/.test(html) &&
    /const\s+ok\s*=\s*await\s+syncTelemetryBackendOnly\s*\(\s*\)/.test(html) &&
    !html.includes('window.audioStudioAlgorithmCostV49?.syncTelemetryBackendOnlyV49'),
  'cost telemetry must use the canonical telemetry-only path without chaining through removed v49 code'
);
assert.ok(hasToken('EVENT_LOG_REFRESH_DEBOUNCE_MS_V69') && hasToken('scheduleEventLogRefreshV69(EVENT_LOG_RUNNING_POLL_MS_V69)'), 'event log refreshes should be debounced and rate-limited during running telemetry ticks');
assert.ok(!hasToken('.finally(()=>refreshEventLogBackendV69())'), 'UI event posting must not trigger an immediate full event-log refresh for every log line');
assert.ok(hasToken('applyZoom(false)'), 'node rebuild should not redraw edges before replacing node DOM');
assert.ok(hasToken('let edgeLayoutRedrawFrame=0') && !compactHtml.includes(compactToken('raf(()=>{ drawEdges(); raf(drawEdges); })')), 'edge redraw after layout should be coalesced instead of triple-drawing every render');
assert.ok(
  hasToken('applyCostTableFinalLayoutV99') &&
    hasToken('renderCostTableSourceV99') &&
    !/new\s+MutationObserver\s*\([^)]*applyCostStoppedIdxFixV62/.test(html) &&
    !/setTimeout\s*\(\s*applyCostStoppedIdxFixV62\s*,\s*0\s*\)/.test(html),
  'cost table should render final six-column layout in one pass without stopped-state async repair churn'
);

console.log('runtime-loop-policy.test passed');
