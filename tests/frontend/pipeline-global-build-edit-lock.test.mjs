import { strict as assert } from 'assert';
import fs from 'fs';

const html = fs.readFileSync(new URL('../../GUI/frontend/index.html', import.meta.url), 'utf8');

function assertIncludes(token, message = `missing token: ${token}`) {
  assert.ok(html.includes(token), message);
}

function functionBlock(name) {
  const start = html.indexOf(`function ${name}`);
  assert.ok(start >= 0, `missing function ${name}`);
  const next = html.indexOf('\n    function ', start + 1);
  return html.slice(start, next > start ? next : undefined);
}

assertIncludes('function runtimeStatusForAllGroups', 'ALL runtime status should aggregate working-group state');
assert.ok(
  /runtimeStatusForAllGroups[\s\S]*RUNTIME_STATES\.RUNNING[\s\S]*RUNTIME_STATES\.PIPE_LOADED[\s\S]*RUNTIME_STATES\.ERROR[\s\S]*RUNTIME_STATES\.NOT_READY/.test(html),
  'ALL runtime status should prefer RUNNING, then PIPE_LOADED, then ERROR, then NOT_READY'
);

assertIncludes('const STRUCTURAL_EDIT_KINDS', 'structural edit kinds should be named explicitly');
const structuralKinds = html.match(/const STRUCTURAL_EDIT_KINDS\s*=\s*new Set\(\[[\s\S]*?\]\);/)?.[0] || '';
for (const kind of [
  'node_added',
  'node_removed',
  'connection_added',
  'connections_removed',
  'selection_deleted',
  'selection_cut',
  'selection_pasted',
  'swap_ports',
  'pipeline_rename',
  'build_affecting_param_updated',
]) {
  assert.ok(structuralKinds.includes(`'${kind}'`), `structural lock should include ${kind}`);
}
for (const layoutKind of ['node_moved', 'nodes_moved', 'auto_arrange', 'manual_zoom_button', 'pinch_zoom']) {
  assert.ok(!structuralKinds.includes(`'${layoutKind}'`), `${layoutKind} should remain a layout/view edit`);
}

assert.ok(
  /runtimeGroupAllowsGraphMutation[\s\S]*runtimeAllowsStructuralEdit\(kind\)/.test(html),
  'graph mutation guard should delegate to the global structural edit lock'
);
assert.ok(
  /renamePipeline[\s\S]*runtimeAllowsStructuralEdit\(['"]pipeline_rename['"]\)/.test(html),
  'pipeline rename should be blocked by the structural lock'
);
assert.ok(
  /function isBuildAffectingParam[\s\S]*function runtimeAllowsBuildAffectingParameterEdit/.test(html),
  'build-affecting parameter edits should have an explicit runtime lock guard'
);
assert.ok(
  /applyValue[\s\S]*runtimeAllowsBuildAffectingParameterEdit\(n,\s*p,\s*['"]build_affecting_param_updated['"]\)/.test(html),
  'inspector parameter writes should check the build-affecting edit lock before writing'
);

const dragBlock = functionBlock('makeDraggable');
assert.ok(!dragBlock.includes('lockedRunDrag'), 'running/loaded state should not block pure node dragging');
assert.ok(dragBlock.includes("backendEdit('node_moved'"), 'node movement should still be recorded as a layout edit');

const autoArrangeBlock = functionBlock('autoArrange');
assert.ok(!/if\s*\(\s*running\s*\)/.test(autoArrangeBlock), 'Auto Arrange should remain available while loaded/running');
assert.ok(autoArrangeBlock.includes("backendEdit('auto_arrange'"), 'Auto Arrange should still persist layout edits');

console.log('pipeline-global-build-edit-lock.test passed');
