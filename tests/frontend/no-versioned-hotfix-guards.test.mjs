import assert from 'node:assert/strict';
import fs from 'node:fs';

const html = fs.readFileSync(new URL('../../frontend/index.html', import.meta.url), 'utf8');
const versionedInstalled = html.match(/__audioStudio[A-Za-z0-9_]*V\d+[A-Za-z0-9_]*Installed/g) || [];
assert.deepEqual([...new Set(versionedInstalled)].sort(), [], 'versioned VxxInstalled hotfix markers must not be kept in production frontend');
assert.ok(!/if\s*\(\s*!\s*window\.__audioStudio/.test(html), 'versioned hotfix install guards must not be kept in production frontend');
assert.ok(!html.includes('__audioStudioBufferDumpV40bInstalled'), 'superseded buffer dump V40b dead code must be removed');
assert.ok(!html.includes('__audioStudioPerAlgorithmCostV49BackendOnlyInstalled'), 'superseded cost V49 dead code must be removed');
console.log('no-versioned-hotfix-guards.test passed');
