import assert from 'node:assert/strict';
import fs from 'node:fs';

const html = fs.readFileSync(new URL('../../frontend/index.html', import.meta.url), 'utf8');

for (const token of [
  'Flexible closed-panel layout occupancy',
  'layout-left-collapsed',
  'layout-right-collapsed',
  'layout-bottom-collapsed',
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
  /--layout-left-col:\s*260px[\s\S]*--layout-right-col:\s*360px[\s\S]*--layout-bottom-row:\s*292px/,
  'app grid should expose left/right/bottom occupancy variables'
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
assert.ok(!/V\d+Installed/.test(html), 'production frontend must not reintroduce versioned Installed guards');

console.log('flexible-panel-layout.test passed');
