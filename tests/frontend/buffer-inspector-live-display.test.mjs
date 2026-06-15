import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const html = readFileSync('GUI/frontend/index.html', 'utf8');

assert(html.includes('makeBufferInspectorUnavailableState'), 'buffer Inspector unavailable state helper is required');
assert(/general:\s*\{\s*format:\s*['"]N\/A['"],\s*rms:\s*null,\s*peak:\s*null/.test(html), 'buffer unavailable General must be N/A/null');
assert(/waveform:\s*\[\]/.test(html), 'buffer unavailable waveform must be empty');
assert(/spectrum:\s*\[\]/.test(html), 'buffer unavailable spectrum must be empty');
assert(/const\s+isLive\s*=\s*!!\([^;]*source\s*===\s*['"]backend['"][^;]*liveMatches/.test(html), 'buffer General must only treat backend matching live frames as live');
console.log('buffer-inspector-live-display.test passed');
