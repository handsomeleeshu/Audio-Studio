import { strict as assert } from 'assert';
import fs from 'fs';

const root = new URL('../../', import.meta.url);
const readText = path => fs.readFileSync(new URL(path, root), 'utf8');
const exists = path => fs.existsSync(new URL(path, root));

const html = readText('GUI/frontend/index.html');
assert.equal((html.match(/<script\b/g) || []).length, 1, 'standalone UI should keep one inline script entry');
assert.ok(!/<script[^>]+src=/i.test(html), 'standalone UI must not load external frontend scripts');
assert.ok(!/<link[^>]+rel=["']?stylesheet/i.test(html), 'standalone UI must not load external CSS bundles');

const deadAssets = [
  'GUI/frontend/assets/js/app.js',
  'GUI/frontend/assets/js/react-app.js',
  'GUI/frontend/assets/js/api.js',
  'GUI/frontend/assets/js/telemetry.js',
  'GUI/frontend/assets/js/pipeline-edit-callbacks.js',
  'GUI/frontend/assets/js/topbar-panel-menu.js',
  'GUI/frontend/assets/css/styles.css',
  'GUI/frontend/assets/css/pipeline-editor-fixes.css',
  'GUI/frontend/assets/css/topbar-legacy.css',
  'GUI/frontend/assets/css/legacy-ui-reset.css',
  'GUI/frontend/assets/css/library-compact.css',
];

for (const asset of deadAssets) {
  assert.ok(!exists(asset), `${asset} is not loaded by GUI/frontend/index.html and should be removed`);
}

const packageJson = JSON.parse(readText('package.json'));
for (const dependency of ['react', 'react-dom', 'vite', '@vitejs/plugin-react']) {
  assert.ok(!((packageJson.dependencies || {})[dependency]), `${dependency} is unused by the standalone frontend`);
}

const guide = readText('docs/frontend_development.md');
for (const stalePhrase of ['React Edition', 'react-app.js', 'UMD CDN', 'npm run dev']) {
  assert.ok(!guide.includes(stalePhrase), `frontend guide should not mention stale React flow: ${stalePhrase}`);
}

console.log('dead-code-policy.test passed');
