import fs from 'node:fs';
import assert from 'node:assert';

const html = fs.readFileSync('frontend/index.html', 'utf8');

assert.ok(html.includes('function algorithmCostItemsForTotalsV101'), 'startup-safe cost item accessor should exist');
assert.ok(html.includes('function isRuntimeRunningForCostTotalsV101'), 'startup-safe running accessor should exist');
assert.ok(html.includes('function backendCostTotalsV100'), 'backend cost totals helper should still exist');

const helperStart = html.indexOf('function algorithmCostItemsForTotalsV101');
const helperEnd = html.indexOf('function syncCostTotalFooterV99');
assert.ok(helperStart > 0 && helperEnd > helperStart, 'safe helpers should sit before cost footer');
const helperBlock = html.slice(helperStart, helperEnd);
assert.ok(helperBlock.includes('try {') && helperBlock.includes('catch (_)'), 'cost totals helper must guard startup TDZ/ReferenceError');
assert.ok(helperBlock.includes('algorithmCostLiveV50') && helperBlock.includes('Array.from(items.values())'), 'cost totals should still read backend-owned cost map when available');
assert.ok(!html.includes('const items = algorithmCostLiveV50?.items'), 'unsafe v100 direct optional access must be removed');
assert.ok(!/V101Installed|v101Installed/.test(html), 'do not introduce versioned installed guards');

console.log('cost-total-startup-safe.test passed');
