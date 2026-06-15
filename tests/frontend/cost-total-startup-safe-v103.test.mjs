import fs from 'fs';
import assert from 'assert';

const html = fs.readFileSync('GUI/frontend/index.html', 'utf8');

assert.ok(html.includes('function algorithmCostItemsForTotalsV103'), 'startup-safe cost item accessor should exist');
assert.ok(html.includes('function isRuntimeRunningForCostTotalsV103'), 'startup-safe running accessor should exist');
assert.ok(html.includes('function backendCostTotalsV100'), 'backend cost totals helper should exist');
assert.ok(html.includes('algorithmCostLiveV50?.items'), 'canonical backend-owned source path should remain visible for policy tests');
assert.ok(!html.includes('const items = algorithmCostLiveV50?.items'), 'unsafe direct optional access must not be used during startup');

const helperStart = html.indexOf('function algorithmCostItemsForTotalsV103');
const footerStart = html.indexOf('function syncCostTotalFooterV99');
const footerEnd = html.indexOf('function applyCostTableFinalLayoutV99', footerStart);
assert.ok(helperStart > 0 && footerStart > helperStart && footerEnd > footerStart, 'safe helpers should be installed before the cost footer');

const helperBlock = html.slice(helperStart, footerStart);
assert.ok(helperBlock.includes('try { live = algorithmCostLiveV50; } catch (_) { return []; }'), 'cost totals helper must guard startup TDZ/ReferenceError');
assert.ok(helperBlock.includes('Array.from(items.values())'), 'cost totals should read backend-owned cost map values when available');

const footer = html.slice(footerStart, footerEnd);
assert.ok(footer.includes('backendCostTotalsV100()'), 'cost footer should use backend cost total helper');
assert.ok(!footer.includes('table.tBodies?.[0]?.rows') && !footer.includes('parseCostNumberV99(row.cells') && !footer.includes('parseCostMemKbV99(row.cells'), 'cost footer must not re-parse rendered DOM rows');
assert.equal((html.match(/function algorithmCostItemsForTotalsV103/g) || []).length, 1, 'safe cost item accessor should be installed once');
assert.equal((html.match(/function backendCostTotalsV100/g) || []).length, 1, 'backend cost total helper should be installed once');
assert.ok(!/V103Installed|v103Installed/.test(html), 'do not introduce versioned installed guards');

console.log('cost-total-startup-safe-v103.test passed');
