import assert from 'node:assert/strict';
import fs from 'node:fs';

const html = fs.readFileSync(new URL('../../frontend/index.html', import.meta.url), 'utf8');

function compactSource(s) {
  return String(s).replace(/\s+/g, ' ');
}

function visibleTextFromHtml(s) {
  return String(s)
    .replace(/<script\b[\s\S]*?<\/script>/gi, ' ')
    .replace(/<style\b[\s\S]*?<\/style>/gi, ' ')
    .replace(/<[^>]+>/g, ' ')
    .replace(/&nbsp;/gi, ' ')
    .replace(/&amp;/gi, '&')
    .replace(/\s+/g, ' ')
    .trim();
}

const source = compactSource(html);
const visibleText = visibleTextFromHtml(html);

// Standalone HTML frontend should be present. These checks intentionally use
// normalized source / visible text rather than exact HTML snippets, so running
// a formatter cannot break the integration test.
assert.ok(/Audio\s+Studio/.test(visibleText), 'standalone Audio Studio UI should be present');
assert.ok(
  /VeriSilicon\s+Advanced\s+Sound\s+System/.test(visibleText),
  'brand subtitle should be present'
);
assert.ok(/class\s*=\s*["'][^"']*brand-logo/.test(source), 'AS logo block should be present');

assert.ok(!/react-app\.js/i.test(html), 'standalone frontend must not load react-app.js');
assert.ok(!/unpkg\.com\/react/i.test(html), 'standalone frontend must not load React from CDN');

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
assert.ok(html.includes('loadPlatformConfig'), 'algorithm library must load platform JSON');
assert.ok(html.includes('undoEdit'), 'pipeline undo should be wired');
assert.ok(html.includes('redoEdit'), 'pipeline redo should be wired');

console.log('plain-html-integration.test passed');
