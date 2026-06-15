import assert from 'node:assert/strict';
import fs from 'node:fs';

const html = fs.readFileSync(new URL('../../GUI/frontend/index.html', import.meta.url), 'utf8');

for (const token of [
  'Flexible closed-panel layout occupancy',
  'layout-left-collapsed',
  'layout-right-collapsed',
  'layout-bottom-collapsed',
  '--layout-center-min',
  'dashboardPanelIdsForLayout',
  'refreshResponsivePanelLayout',
  'scheduleResponsivePanelLayout',
  'setupResponsivePanelLayout',
  'audioStudioPanelVisibilityChanged',
]) {
  assert.ok(html.includes(token), `missing flexible panel layout token: ${token}`);
}

assert.match(
  html,
  /--layout-left-col:\s*260px[\s\S]*--layout-center-min:\s*600px[\s\S]*--layout-right-col:\s*360px[\s\S]*--layout-bottom-row:\s*292px/,
  'app grid should expose left/right/bottom occupancy variables'
);
assert.match(
  html,
  /grid-template-columns:\s*var\(--layout-left-col\)\s+minmax\(var\(--layout-center-min\),\s*1fr\)\s+var\(--layout-right-col\)/,
  'center grid minimum should remain responsive through a CSS variable'
);
const flexibleStyleStart = html.indexOf('/* Flexible closed-panel layout occupancy. */');
const collapsedStyleStart = html.indexOf('.app.layout-left-collapsed', flexibleStyleStart);
assert.ok(flexibleStyleStart >= 0 && collapsedStyleStart > flexibleStyleStart, 'flexible panel style block should be present');
const flexibleStyleBlock = html.slice(flexibleStyleStart, collapsedStyleStart);
assert.ok(
  !/grid-template-(?:columns|rows):[^;]+!important/.test(flexibleStyleBlock),
  'flexible panel grid should not use !important because it would override responsive media queries'
);

assert.match(
  html,
  /@media\(max-width:1320px\)\s*{[\s\S]*?\.app\s*{[\s\S]*?--layout-left-col:\s*230px[\s\S]*?--layout-center-min:\s*500px[\s\S]*?--layout-right-col:\s*320px/,
  'flexible grid variables should preserve the existing narrow-window column sizes'
);
assert.match(
  html,
  /@media\(max-height:760px\)\s*{[\s\S]*?\.app\s*{[\s\S]*?--layout-bottom-row:\s*250px/,
  'flexible grid variables should preserve the existing short-window bottom row height'
);

assert.match(
  html,
  /\.app\.layout-left-collapsed\s*{[\s\S]*--layout-left-col:\s*0px/,
  'closing Algorithm Library should release the left grid column'
);
assert.match(
  html,
  /\.app\.layout-right-collapsed\s*{[\s\S]*--layout-right-col:\s*0px/,
  'closing Inspector should release the right grid column'
);
assert.match(
  html,
  /\.app\.layout-bottom-collapsed\s*{[\s\S]*--layout-bottom-row:\s*0px/,
  'closing all dashboard panels should release the bottom grid row'
);
assert.ok(
  html.includes("'costPanel', 'corePanel', 'probePanel', 'healthPanel', 'meterPanel', 'logPanel'") ||
    html.includes('costPanel') && html.includes('corePanel') && html.includes('probePanel') && html.includes('healthPanel') && html.includes('meterPanel') && html.includes('logPanel'),
  'all dashboard cards, including Event Log, should participate in bottom occupancy detection'
);

const refreshStart = html.indexOf('function refreshResponsivePanelLayout');
const scheduleStart = html.indexOf('function scheduleResponsivePanelLayout');
assert.ok(refreshStart >= 0 && scheduleStart > refreshStart, 'responsive layout functions should be present');
const refreshBlock = html.slice(refreshStart, scheduleStart);
assert.ok(refreshBlock.includes('layoutChanged'), 'responsive layout should detect whether the grid classes actually changed');
assert.ok(refreshBlock.includes('if (!layoutChanged) return'), 'responsive layout should avoid canvas redraw work when occupancy is unchanged');
assert.ok(
  !refreshBlock.includes("backendTool('responsive_panel_layout'"),
  'responsive layout refresh must not POST a backend tool event during UI-only layout changes'
);

const setupStart = html.indexOf('function setupResponsivePanelLayout');
assert.ok(setupStart >= 0, 'responsive layout setup should be present');
const setupEnd = html.indexOf("\n\n    window.addEventListener('resize'", setupStart);
assert.ok(setupEnd > setupStart, 'responsive layout setup block should be followed by the existing top-level resize handler');
const setupBlock = html.slice(setupStart, setupEnd);
assert.ok(
  setupBlock.includes("window.addEventListener('audioStudioPanelVisibilityChanged'"),
  'responsive layout should listen on window because setPanelVisible dispatches the event on window'
);
assert.ok(
  !setupBlock.includes("window.addEventListener('resize'"),
  'responsive layout should not add a second resize listener on top of renderAll resize handling'
);
assert.ok(
  html.indexOf('setupResponsivePanelLayout();') > html.indexOf('window.audioStudioDashboardStripV47'),
  'responsive layout setup should run after dashboard strip initialization so the initial occupancy is accurate'
);
assert.ok(!/V\d+Installed/.test(html), 'production frontend must not reintroduce versioned Installed guards');

console.log('flexible-panel-layout.test passed');
