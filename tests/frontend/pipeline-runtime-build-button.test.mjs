import { strict as assert } from 'assert';
import fs from 'fs';

const html = fs.readFileSync(new URL('../../GUI/frontend/index.html', import.meta.url), 'utf8');

function assertIncludes(token, message = `missing token: ${token}`) {
  assert.ok(html.includes(token), message);
}

assertIncludes('function updateBuildRuntimeButtonState', 'Build button state should be derived from target runtime group state');
assertIncludes('btnRuntimeUnload', 'PIPE_LOADED should use a distinct unload color class without moving the button');
assert.ok(
  /RUNTIME_STATES\.PIPE_LOADED[\s\S]*textContent\s*=\s*['"]▣ Unload['"][\s\S]*classList\.add\(['"]btnRuntimeUnload['"]\)/.test(html),
  'PIPE_LOADED target groups should render the Build button as Unload with the unload class'
);
assert.ok(
  /buildingRuntimeGroupId[\s\S]*buildBtn\.disabled\s*=\s*true[\s\S]*▣ Building/.test(html),
  'the transient building group should disable the button and show Building'
);
assert.ok(
  /RUNTIME_STATES\.RUNNING[\s\S]*buildBtn\.disabled\s*=\s*true[\s\S]*▣ Running/.test(html),
  'RUNNING target groups should disable the button and show Running'
);
assert.ok(
  /#buildBtn'\)\.addEventListener\('click'[\s\S]*runtimeStatusForGroupId\(['"]ALL['"]\)[\s\S]*RUNTIME_STATES\.PIPE_LOADED[\s\S]*unloadRuntimeGroup\(['"]ALL['"]\)[\s\S]*buildRuntimeGroup\(['"]ALL['"]\)/.test(html),
  'Build button click should use the global ALL runtime target for build/unload'
);

assert.ok(
  /const buildPayload = \{[\s\S]*\.\.\.pipelineSnapshot\(\)[\s\S]*build_scope:\s*['"]all_pipelines['"][\s\S]*group_id:\s*['"]ALL['"][\s\S]*\}/.test(html),
  'build payload should force all-pipelines scope and group_id ALL regardless of selected pipeline'
);
assert.ok(
  /apiPost\(['"]\/api\/pipeline\/build['"],\s*buildPayload\)/.test(html),
  'build action should POST the explicit global build payload'
);

assertIncludes('async function unloadRuntimeGroup', 'frontend should own an unload action');
assertIncludes("apiPost('/api/pipeline/unload'", 'unload action should POST /api/pipeline/unload');
assert.ok(
  /unloadRuntimeGroup[\s\S]*pipelineSnapshot\(\)[\s\S]*unload_scope:\s*['"]all_pipelines['"][\s\S]*group_id:\s*['"]ALL['"][\s\S]*target_group/.test(html),
  'unload payload should reuse pipelineSnapshot shape and force global all-pipelines target information'
);
assert.ok(
  /res && res\.ok === true[\s\S]*normalizeRuntimeStatus\(res\?\.runtime_state\) === RUNTIME_STATES\.NOT_READY/.test(html),
  'unload success should require ok:true and runtime_state NOT_READY'
);
assert.ok(
  /setRuntimeStatusForGroup\(['"]ALL['"],\s*RUNTIME_STATES\.NOT_READY,\s*['"]unload_success['"]\)/.test(html),
  'successful unload should set every working group to NOT_READY'
);

assertIncludes('function upsertUpdatedPipelineFromBuildResponse', 'build responses should upsert backend-generated pipeline data');
assert.ok(
  /upsertUpdatedPipelineFromBuildResponse\(res\)/.test(html),
  'backend-updated project data should be applied even before compile success is evaluated'
);
assert.ok(
  html.includes('updated_pipelines') &&
    html.includes('updated_module_instances') &&
  /findIndex\(p => String\(p\?\.pipe_id/.test(html) &&
    /findIndex\(p => String\(p\?\.name/.test(html),
  'updated_pipelines should upsert by pipe_id first and name fallback, and module_instances should refresh'
);

assert.ok(
  /const backendOk = res && res\.ok === true/.test(html) &&
    /const pipeLoaded = normalizeRuntimeStatus\(res\?\.runtime_state\) === RUNTIME_STATES\.PIPE_LOADED/.test(html) &&
    /const ok = backendOk && pipeLoaded/.test(html),
  'build success should remain strict: response exists, ok true, and PIPE_LOADED'
);

assertIncludes('function runtimeGroupAllowsGraphMutation', 'graph mutations should be guarded by per-group runtime state');
assert.ok(
  /function runtimeAllowsStructuralEdit[\s\S]*RUNTIME_STATES\.PIPE_LOADED[\s\S]*RUNTIME_STATES\.RUNNING[\s\S]*toast/.test(html),
  'loaded/running global state should block structural edits with a user-visible warning'
);
assert.ok(
  !/st === RUNTIME_STATES\.PIPE_LOADED \|\| st === RUNTIME_STATES\.ERROR\) setRuntimeStatusForGroup\(id, RUNTIME_STATES\.NOT_READY/.test(html),
  'graph mutation invalidation must not silently reset PIPE_LOADED/ERROR groups to NOT_READY'
);

console.log('pipeline-runtime-build-button.test passed');
