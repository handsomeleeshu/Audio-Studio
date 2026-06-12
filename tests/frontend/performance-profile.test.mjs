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
  '__audioStudioProfileReset',
  '__audioStudioProfileInteractionStop',
  '/api/runtime/run',
  'profiles/frontend'
]) {
  assert.ok(source.includes(phrase), `profile harness should collect ${phrase}`);
}

assert.ok(
  /await\s+evaluate\s*\(\s*client\s*,\s*sessionId\s*,\s*['"]window\.__audioStudioProfileReset\(\)['"]\s*\)/.test(source),
  'profile harness should reset page counters after warmup so long-task/frame metrics describe the measured window'
);
assert.ok(
  source.includes("scenario === 'interaction'") && source.includes('startInteractionDriver'),
  'profile harness should include an interaction scenario for scroll and selection responsiveness'
);

console.log('performance-profile.test passed');
