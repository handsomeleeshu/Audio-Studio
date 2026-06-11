import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const html = readFileSync('frontend/index.html', 'utf8');
const backend = readFileSync('backend/src/http_server.cpp', 'utf8');

for (const forbidden of [
  "mode: 'visual_fallback_only'",
  'mode: "visual_fallback_only"',
  "source: 'visual_fallback'",
  'source: "visual_fallback"',
  'makeFrontendFallbackInspectorLiveData',
  "source: 'frontend_fallback'",
  'source: "frontend_fallback"',
]) {
  assert(!html.includes(forbidden), `frontend must not contain ${forbidden}`);
}

assert(html.includes('function makeInspectorUnavailableLiveData'), 'node Inspector must have backend-unavailable state builder');
assert(html.includes("source: 'unavailable'") || html.includes('source: "unavailable"'), 'frontend should represent unavailable backend state explicitly');
assert(/function\s+edgeSampleRateLabelForEdge[\s\S]*fmt\.source\s*!==\s*['"]backend['"][\s\S]*return\s+''/.test(html), 'edge sample-rate labels must only use backend-owned cached formats');
assert(!/edgeSampleRateLabelForEdge[\s\S]{0,600}inferredEdgeFormat/.test(html), 'edge sample-rate label must not call inferredEdgeFormat fallback');
assert(backend.includes('/api/runtime/buffer/formats/live'), 'backend must expose runtime buffer format live API');
console.log('backend-owned-live-data.test passed');
