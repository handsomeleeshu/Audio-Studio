import fs from 'node:fs';
import assert from 'node:assert';

const html = fs.readFileSync('frontend/index.html', 'utf8');

assert.ok(html.includes('function renderSelectionChange'), 'selection-only rendering helper should exist');

for (const match of html.matchAll(/renderAll\(false\);/g)) {
  const ctx = html.slice(Math.max(0, match.index - 900), Math.min(html.length, match.index + 450));
  assert.ok(!/(selectionBox|selection-box|selectionRect|marquee|rectsIntersect|rubberBand)/.test(ctx), 'marquee/selection-only path must not call full renderAll(false)');
}

assert.ok(html.includes('function backendCostTotalsV100') && html.includes('algorithmCostLiveV50?.items') && html.includes('Array.from(items.values())'), 'cost total footer should aggregate directly from backend-owned algorithmCostLiveV50.items');

const footerStart = html.indexOf('function syncCostTotalFooterV99');
const footerEnd = html.indexOf('function applyCostTableFinalLayoutV99');
assert.ok(footerStart > 0 && footerEnd > footerStart, 'cost footer and layout functions should be present');
const footer = html.slice(footerStart, footerEnd);

assert.ok(!footer.includes('table.tBodies?.[0]?.rows') && !footer.includes('parseCostNumberV99(row.cells') && !footer.includes('parseCostMemKbV99(row.cells'), 'cost total footer must not re-parse rendered DOM rows');
assert.ok(footer.includes('backendCostTotalsV100()') && footer.includes('escapeHtml(totals.cpu)') && footer.includes('escapeHtml(totals.mem)'), 'cost total footer should render totals from backend cost map');
assert.ok(!/new\s+MutationObserver\s*\([^)]*applyCostStoppedIdxFixV62/.test(html) && !/setTimeout\s*\(\s*applyCostStoppedIdxFixV62\s*,\s*0\s*\)/.test(html), 'cost table final layout should not reintroduce async stopped-state DOM repair churn');
assert.ok(!/V100Installed|v100Installed/.test(html), 'do not introduce versioned installed guards');

console.log('cost-table-data-total-and-selection.test passed');
