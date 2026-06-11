import assert from 'node:assert/strict';
import fs from 'node:fs';

const root = new URL('../../', import.meta.url);
const readText = path => fs.readFileSync(new URL(path, root), 'utf8');
const exists = path => fs.existsSync(new URL(path, root));

const html = readText('frontend/index.html');
assert.equal((html.match(/<script\b/g) || []).length, 1, 'standalone UI should keep one inline script entry');
assert.ok(!/<script[^>]+src=/i.test(html), 'standalone UI must not load external frontend scripts');
assert.ok(!/<link[^>]+rel=["']?stylesheet/i.test(html), 'standalone UI must not load external CSS bundles');

const deadAssets = [
  'frontend/assets/js/app.js',
  'frontend/assets/js/react-app.js',
  'frontend/assets/js/api.js',
  'frontend/assets/js/telemetry.js',
  'frontend/assets/js/pipeline-edit-callbacks.js',
  'frontend/assets/js/topbar-panel-menu.js',
  'frontend/assets/css/styles.css',
  'frontend/assets/css/pipeline-editor-fixes.css',
  'frontend/assets/css/topbar-legacy.css',
  'frontend/assets/css/legacy-ui-reset.css',
  'frontend/assets/css/library-compact.css',
];

for (const asset of deadAssets) {
  assert.ok(!exists(asset), `${asset} is not loaded by frontend/index.html and should be removed`);
}

const packageJson = JSON.parse(readText('package.json'));
for (const dependency of ['react', 'react-dom', 'vite', '@vitejs/plugin-react']) {
  assert.ok(!packageJson.dependencies?.[dependency], `${dependency} is unused by the standalone frontend`);
}

const guide = readText('docs/frontend_development.md');
for (const stalePhrase of ['React Edition', 'react-app.js', 'UMD CDN', 'npm run dev']) {
  assert.ok(!guide.includes(stalePhrase), `frontend guide should not mention stale React flow: ${stalePhrase}`);
}

console.log('dead-code-policy.test passed');
