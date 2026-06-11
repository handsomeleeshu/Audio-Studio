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

console.log('runtime-loop-policy.test passed');
