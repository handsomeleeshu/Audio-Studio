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
  'Ctrl+Z',
  'apiPost',
  '/api/pipeline/edit',
  '/api/project/save',
]) {
  assert.ok(html.includes(token), `missing ${token}`);
}

assert.ok(html.includes('id="undoBtn"'), 'Undo button should exist');
assert.ok(html.includes('id="redoBtn"'), 'Redo button should exist');
console.log('standalone-features.test passed');
