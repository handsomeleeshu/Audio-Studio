import assert from 'node:assert/strict';
import fs from 'node:fs';

const html = fs.readFileSync(new URL('../../GUI/frontend/index.html', import.meta.url), 'utf8');

for (const token of [
  'Closed-panel boundary gutters',
  '--layout-edge-gutter: 10px',
  'margin-left: var(--layout-edge-gutter) !important',
  'margin-right: var(--layout-edge-gutter) !important',
  'border-left: 1px solid var(--line) !important',
  'border-right: 1px solid var(--line) !important',
]) {
  assert.ok(html.includes(token), `missing closed-panel gutter token: ${token}`);
}

assert.match(
  html,
  /\.app\.layout-left-collapsed\s+\.canvas-zone,\s*\.app\.layout-left-collapsed\s+\.bottom\s*{[\s\S]*?margin-left:\s*var\(--layout-edge-gutter\)\s*!important/,
  'canvas and bottom dashboard should keep a left gutter when Algorithm Library is closed'
);

assert.match(
  html,
  /\.app\.layout-right-collapsed\s+\.canvas-zone,\s*\.app\.layout-right-collapsed\s+\.bottom\s*{[\s\S]*?margin-right:\s*var\(--layout-edge-gutter\)\s*!important/,
  'canvas and bottom dashboard should keep a right gutter when Inspector is closed'
);

assert.ok(!/V\d+Installed/.test(html), 'production frontend must not reintroduce versioned Installed guards');

console.log('closed-panel-boundary-gutter.test passed');
