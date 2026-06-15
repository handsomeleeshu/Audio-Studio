import assert from 'node:assert/strict';
import fs from 'node:fs';

const html = fs.readFileSync(new URL('../../GUI/frontend/index.html', import.meta.url), 'utf8');

for (const token of [
  'Pipeline-scoped runtime state model',
  'pipelineRuntimeStates',
  'runtime-not-ready',
  'runtime-validated',
  'runtime-built',
  'runtime-running',
  'runtime-stopped',
  'edgePipelineRuntimeState',
  'installPipelineScopedRuntimeControls',
  'startRuntimeGroup',
  'stopRuntimeGroup',
  'setRuntimeGroupFilter',
  'audioStudioPipelineRuntimeState',
  'all_working_groups',
  'working_group',
]) {
  assert.ok(html.includes(token), `missing pipeline scoped runtime token: ${token}`);
}

assert.ok(
  html.includes('if (edgePipelineRuntimeState(ep) === \'running\')') ||
    html.includes('if(edgePipelineRuntimeState(ep)===\'running\')'),
  'edge animation and sample-rate labels should be gated by per-edge pipeline runtime state'
);
assert.ok(
  html.includes('runtime-${nodeRuntimeVisualState(n)}') || html.includes('nodeRuntimeVisualState'),
  'node render path should carry per-node runtime status classes'
);
assert.ok(
  !/V\d+Installed/.test(html),
  'production frontend must not reintroduce versioned Installed guards'
);

console.log('pipeline-scoped-runtime-state.test passed');
