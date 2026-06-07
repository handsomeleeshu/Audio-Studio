import assert from 'node:assert/strict';
import fs from 'node:fs';

const html = fs.readFileSync(new URL('../../frontend/index.html', import.meta.url), 'utf8');

assert.ok(html.includes('AUDIO STUDIO'), 'standalone HTML UI should be present');
assert.ok(!/react/i.test(html), 'frontend/index.html must not reference React');
assert.ok(!html.includes('react-app.js'), 'standalone frontend must not load react-app.js');

for (const endpoint of [
  '/api/pipeline/validate',
  '/api/pipeline/build',
  '/api/runtime/run',
  '/api/runtime/stop',
  '/api/telemetry',
  '/api/pipeline/edit',
  '/api/pipeline/tool',
  '/api/param/update',
  '/api/node/action',
  '/api/project/save',
]) {
  assert.ok(html.includes(endpoint), `missing backend endpoint ${endpoint}`);
}

assert.ok(html.includes('syncTelemetryFromBackend'), 'telemetry must be pulled from backend');
assert.ok(html.includes('backendEdit'), 'pipeline editing callbacks must be wired');
console.log('plain-html-integration.test passed');
