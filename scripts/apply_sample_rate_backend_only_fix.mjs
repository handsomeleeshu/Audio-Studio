import fs from 'node:fs';

function replaceOnce(text, from, to, label) {
  if (!text.includes(from)) {
    throw new Error(`Missing patch target: ${label}`);
  }
  return text.replace(from, to);
}

const indexPath = 'frontend/index.html';
let html = fs.readFileSync(indexPath, 'utf8');

html = replaceOnce(
  html,
  "      normalized.source = fmt && (fmt.source || fmt.backendOwned || fmt.backend_owned) ? 'backend' : (normalized.source || ''); edgeRuntimeFormatCache.set(key, normalized);",
  `      normalized.source = fmt && (fmt.source || fmt.backendOwned || fmt.backend_owned) ? 'backend' : (normalized.source || '');
      const backendSampleRate = Number(fmt?.sampleRate ?? fmt?.sample_rate);
      if (normalized.source === 'backend' && Number.isFinite(backendSampleRate) && backendSampleRate > 0) {
        normalized.backendSampleRate = backendSampleRate;
      } else {
        delete normalized.backendSampleRate;
      }
      edgeRuntimeFormatCache.set(key, normalized);`,
  'rememberEdgeRuntimeFormat backend sample-rate ownership'
);

html = replaceOnce(
  html,
  `    function edgeSampleRateLabelForEdge(ep, key = '') {
      const fmt = (key ? cachedEdgeRuntimeFormat(key) : null) || inferredEdgeFormat(ep);
      const rate = Number(fmt?.sampleRate || fmt?.sample_rate || 48000);
      return formatSampleRateLabel(Number.isFinite(rate) && rate > 0 ? rate : 48000);
    }`,
  `    function edgeSampleRateLabelForEdge(ep, key = '') {
      const fmt = key ? cachedEdgeRuntimeFormat(key) : null;
      if (!fmt || fmt.source !== 'backend') return 'N/A';
      const rate = Number(fmt.backendSampleRate);
      return Number.isFinite(rate) && rate > 0 ? formatSampleRateLabel(rate) : 'N/A';
    }`,
  'backend-only edge sample-rate label'
);

fs.writeFileSync(indexPath, html);

const testPath = 'tests/frontend/pipeline-selection-buffer-edge.test.mjs';
let test = fs.readFileSync(testPath, 'utf8');

test = replaceOnce(
  test,
  "assert.ok(/function edgeSampleRateLabelForEdge[\\s\\S]*inferredEdgeFormat\\(ep\\)/.test(html), 'sample-rate label should fall back to inferred format when backend format is not cached');",
  `const sampleRateHelper = html.slice(html.indexOf('function edgeSampleRateLabelForEdge'), html.indexOf('function setupPanelMenuAutoClose'));
assert.ok(sampleRateHelper.includes("if (!fmt || fmt.source !== 'backend') return 'N/A';"), 'sample-rate label should show N/A when backend format is unavailable');
assert.ok(sampleRateHelper.includes("return Number.isFinite(rate) && rate > 0 ? formatSampleRateLabel(rate) : 'N/A';"), 'sample-rate label should show N/A when backend rate is invalid');
assert.ok(!sampleRateHelper.includes('inferredEdgeFormat(ep)'), 'sample-rate label must not fall back to inferred frontend format');
assert.ok(!sampleRateHelper.includes('48000'), 'sample-rate label helper must not default to synthetic 48 kHz');
assert.ok(html.includes('normalized.backendSampleRate = backendSampleRate'), 'backend sample-rate should be recorded only from raw backend API fields');`,
  'sample-rate policy assertions'
);

fs.writeFileSync(testPath, test);

fs.rmSync('scripts/apply_sample_rate_backend_only_fix.mjs', { force: true });
fs.rmSync('.github/workflows/apply-sample-rate-backend-only.yml', { force: true });
