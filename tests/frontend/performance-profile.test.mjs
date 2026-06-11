import assert from 'node:assert/strict';
import fs from 'node:fs';

const root = new URL('../../', import.meta.url);
const packageJson = JSON.parse(fs.readFileSync(new URL('package.json', root), 'utf8'));
const profileScriptUrl = new URL('tests/performance/profile_frontend.mjs', root);

assert.equal(
  packageJson.scripts?.['profile:frontend'],
  'node tests/performance/profile_frontend.mjs',
  'package.json must expose npm run profile:frontend'
);

assert.ok(fs.existsSync(profileScriptUrl), 'frontend performance profile harness must exist');

const source = fs.readFileSync(profileScriptUrl, 'utf8');
for (const phrase of [
  'Performance.getMetrics',
  'longtask',
  'requestAnimationFrame',
  '/api/runtime/run',
  'profiles/frontend'
]) {
  assert.ok(source.includes(phrase), `profile harness should collect ${phrase}`);
}

console.log('performance-profile.test passed');
