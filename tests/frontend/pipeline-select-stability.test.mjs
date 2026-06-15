import { strict as assert } from 'assert';
import fs from 'fs';

const html = fs.readFileSync(new URL('../../GUI/frontend/index.html', import.meta.url), 'utf8');

for (const token of [
  'pipelineSelectInteractionState',
  'markPipelineSelectInteraction',
  'pipelineSelectIsBeingEdited',
  'setupPipelineSelectInteractionGuards',
  'audioStudioPipelineSelectInteractionGuard',
  'pipelineSelectInteractionBound',
  'pipelineSelectInteracting',
]) {
  assert.ok(html.includes(token), `missing pipeline select stability token: ${token}`);
}

assert.ok(
  html.includes("select.dataset.mode === 'working_groups_filter_v51'") &&
    html.includes('return currentWorkingGroupsV44();'),
  'legacy v44 pipeline select renderer should not rebuild the dropdown after v51 owns it'
);

assert.ok(
  /pipelineSelectIsBeingEdited\(select\)[\s\S]{0,140}return groups/.test(html),
  'v51 pipeline select renderer should skip option rebuild/value forcing while the user is interacting with the native dropdown'
);

assert.ok(
  /audioStudioPipelineSelectInteractionGuard\?\.isActive\?\.\(select\)[\s\S]{0,80}return/.test(html),
  'runtime status decoration should not rewrite option text while the dropdown is open'
);

assert.ok(
  !/if\s*\(\s*running\s*\)[\s\S]{0,140}pipeline group/i.test(html),
  'pipeline view switching must not be blocked by runtime state'
);

assert.ok(!/V\d+Installed/.test(html), 'production frontend must not reintroduce versioned Installed guards');

console.log('pipeline-select-stability.test passed');
