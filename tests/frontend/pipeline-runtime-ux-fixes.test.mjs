import { strict as assert } from 'assert';
import fs from 'fs';

const html = fs.readFileSync(new URL('../../GUI/frontend/index.html', import.meta.url), 'utf8');
const http = fs.readFileSync(new URL('../../GUI/backend/src/http_server.cpp', import.meta.url), 'utf8');

for (const token of [
  'Pipeline runtime UX helper functions',
  'edgeRuntimeFormatCache',
  'rememberEdgeRuntimeFormat',
  'edgeSampleRateLabelForEdge',
  'formatSampleRateLabel',
  'setupPanelMenuAutoClose',
  'refreshBackendNotifications',
  'setupBackendNotificationPolling',
  '/api/ui/notify',
]) {
  assert.ok(html.includes(token), `missing frontend token: ${token}`);
}

assert.ok(!html.includes("if (idx % 2 === 0) { const pa = portPoint(a, 'out', ep.fromPort), pb = portPoint(b, 'in', ep.toPort);"), 'edge sample-rate labels must not be limited to every other edge');
assert.ok(/if\s*\(running\)\s*\{[\s\S]*edgeSampleRateLabelForEdge/.test(html), 'running edges should render sample-rate labels from runtime format');
assert.ok(/function\s+toast\s*\(\s*msg\s*,\s*level\s*=\s*['"]info['"]\s*\)/.test(html), 'toast should support severity levels');
assert.ok(/\.toast\s*\{[\s\S]*top:\s*156px/.test(html), 'toast should be moved downward');
assert.ok(/\.pipeline-node\.locked\s*\{[\s\S]*cursor:\s*pointer/.test(html), 'running nodes should not show a forbidden cursor on hover');
assert.ok(html.includes('lockedRunDrag') && html.includes('Run 状态下锁定 pipeline，请 Stop 后再拖动节点'), 'running edit warning should be delayed until actual drag attempt');
assert.ok(html.includes('menu.classList.remove(\'show\')') || html.includes('menu.classList.remove("show")'), 'panel menu should close on outside click');

assert.ok(http.includes('jsonStringField'), 'backend should parse notification level/message fields');
assert.ok(http.includes('ui_notifications'), 'backend should store queued UI notifications');
assert.ok(http.includes('req.method == "GET" && req.path == "/api/ui/notify"'), 'backend should expose GET /api/ui/notify');
assert.ok(http.includes('req.method == "POST" && req.path == "/api/ui/notify"'), 'backend should expose POST /api/ui/notify');
assert.ok(!/V\d+Installed/.test(html), 'production frontend must not reintroduce versioned Installed guards');

console.log('pipeline-runtime-ux-fixes.test passed');
