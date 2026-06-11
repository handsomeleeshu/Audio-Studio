import assert from 'node:assert/strict';
import fs from 'node:fs';

const html = fs.readFileSync(new URL('../../frontend/index.html', import.meta.url), 'utf8');

// Regression coverage for the v85/v86 cumulative runtime visibility fix.
// The important behavior is that runtime state must decorate the graph only;
// pipeline visibility remains owned by the existing working-group filter.
assert.ok(!/V\d+Installed/.test(html), 'production frontend must not reintroduce versioned Installed guards');

assert.ok(
  html.includes('node-name') &&
    /text-overflow\s*:\s*ellipsis/.test(html) &&
    /white-space\s*:\s*nowrap/.test(html),
  'long algorithm names should be ellipsized and must not cover the node status dot'
);

assert.ok(
  html.includes('working_groups_filter_v51') || html.includes('visibleNodeIdSetV51'),
  'pipeline visibility should remain owned by the working-group filter path'
);

assert.ok(
  !/runtime[^\n]{0,80}state\.nodes\s*=\s*[^;]*filter/i.test(html) &&
    !/state\.nodes\s*=\s*[^;]*runtime/i.test(html),
  'runtime-state code must not filter state.nodes; otherwise other pipeline groups can disappear'
);

assert.ok(
  !/Run 状态下不能切换 pipeline group/.test(html),
  'pipeline group view switching should remain allowed while a pipeline is running'
);

console.log('pipeline-runtime-visibility-hotfix.test passed');
