import assert from 'node:assert/strict';
import fs from 'node:fs';

const html = fs.readFileSync(new URL('../../GUI/frontend/index.html', import.meta.url), 'utf8');

assert.ok(
  html.includes('Running pipeline view switch hotfix'),
  'missing running pipeline view switch hotfix marker'
);
assert.ok(
  !html.includes('Run 状态下不能切换 pipeline group，请先 Stop'),
  'pipeline dropdown must allow switching views while one pipeline group is running'
);
assert.ok(
  !/if\s*\(\s*running\s*\)\s*\{\s*toast\(\s*['"]Run 状态下不能切换 pipeline group，请先 Stop/.test(html),
  'running-state dropdown change guard must be removed'
);
assert.ok(
  html.includes('activeWorkingGroupIdV51 = e.target.value || \'ALL\'') ||
    html.includes('activeWorkingGroupIdV51=e.target.value||\'ALL\''),
  'canonical v51 group filter should remain responsible for switching All vs one group'
);
assert.ok(
  html.includes('Pipeline runtime visibility hotfix') && html.includes('function withRuntimeVisibleGraph(fn) { return fn(); }'),
  'runtime state must not filter state.nodes/state.edges while rendering'
);
assert.ok(
  html.includes('class="node-name"') && html.includes('text-overflow: ellipsis'),
  'long node names should remain ellipsized before the status dot'
);
assert.ok(!/V\d+Installed/.test(html), 'production frontend must not reintroduce versioned Installed guards');

console.log('pipeline-running-view-switch-hotfix.test passed');
