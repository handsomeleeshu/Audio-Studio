import assert from 'node:assert/strict';
import fs from 'node:fs';

const html = fs.readFileSync(new URL('../../frontend/index.html', import.meta.url), 'utf8');

for (const token of [
  'Bottom dashboard edge gutters',
  '.app .bottom',
  'margin-right: var(--layout-edge-gutter, 10px) !important',
  'border-right: 1px solid var(--line) !important',
  '.app.layout-left-collapsed .bottom',
  'margin-left: var(--layout-edge-gutter, 10px) !important',
]) {
  assert.ok(html.includes(token), `missing bottom dashboard edge gutter token: ${token}`);
}

assert.match(
  html,
  /\.app\s+\.bottom\s*{[\s\S]*?margin-right:\s*var\(--layout-edge-gutter,\s*10px\)\s*!important[\s\S]*?border-right:\s*1px\s+solid\s+var\(--line\)\s*!important/,
  'bottom dashboard should keep a right gutter even when Inspector is open'
);

assert.match(
  html,
  /\.app\.layout-left-collapsed\s+\.bottom\s*{[\s\S]*?margin-left:\s*var\(--layout-edge-gutter,\s*10px\)\s*!important[\s\S]*?border-left:\s*1px\s+solid\s+var\(--line\)\s*!important/,
  'bottom dashboard should keep a left gutter when Algorithm Library is closed'
);

assert.ok(!/V\d+Installed/.test(html), 'production frontend must not reintroduce versioned Installed guards');

console.log('bottom-dashboard-edge-gutter.test passed');
