import assert from 'node:assert/strict';
import fs from 'node:fs';

const html = fs.readFileSync(new URL('../../frontend/index.html', import.meta.url), 'utf8');

assert.ok(html.includes('Audio Studio'), 'new top-left brand should be Audio Studio');
assert.ok(html.includes('VeriSilicon Advanced Sound System'), 'brand subtitle should be full VASS name');
assert.ok(html.includes('brand-logo"><span>AS</span>'), 'AS logo should be present');
assert.ok(!/react-app\.js/i.test(html), 'standalone UI must not load React app');

for (const token of [
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
]) {
  assert.ok(html.includes(token), `missing ${token}`);
}

// The UI may show different human-readable shortcut labels, so test the real
// key handling code instead of requiring a brittle literal "Ctrl+Z" string.
assert.ok(
  html.includes("e.key.toLowerCase()==='z'") && html.includes('undoEdit()'),
  'missing Ctrl/Cmd+Z undo key handling'
);
assert.ok(
  html.includes("e.key.toLowerCase()==='y'") && html.includes('redoEdit()'),
  'missing Ctrl/Cmd+Y redo key handling'
);

assert.ok(html.includes('id="undoBtn"'), 'Undo button should exist');
assert.ok(html.includes('id="redoBtn"'), 'Redo button should exist');
console.log('standalone-features.test passed');
