import { strict as assert } from 'assert';
import fs from 'fs';

const html = fs.readFileSync(new URL('../../GUI/frontend/index.html', import.meta.url), 'utf8');

for (const token of [
  'Pipeline-scoped runtime state model',
  'pipelineRuntimeStates',
  'runtime-not-ready',
  'runtime-pipe-unloaded',
  'runtime-pipe-loaded',
  'runtime-running',
  'runtime-error',
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
  html.includes('if (edgePipelineRuntimeState(ep) === RUNTIME_STATES.PIPE_RUNNING)') ||
    html.includes('if(edgePipelineRuntimeState(ep)===RUNTIME_STATES.PIPE_RUNNING)'),
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
