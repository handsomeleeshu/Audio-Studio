import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const html = readFileSync('GUI/frontend/index.html', 'utf8');
const compact = html.replace(/\s+/g, '');

assert(compact.includes('syncTelemetryBackendOnly,backendOnly:true'), 'runtime loop policy requires syncTelemetryBackendOnly and backendOnly:true to stay adjacent');
assert(html.includes('nodeCardCostForV98'), 'node card cost accessor must still be exported');
assert(!compact.includes('syncTelemetryBackendOnly,nodeCardCostForV98,backendOnly:true'), 'nodeCardCostForV98 must not be inserted between syncTelemetryBackendOnly and backendOnly:true');
assert(/window\.audioStudioAlgorithmCostV50\s*=\s*\{[^}]*syncTelemetryBackendOnly\s*,\s*backendOnly:\s*true\s*,\s*nodeCardCostForV98[^}]*\}/.test(html), 'audioStudioAlgorithmCostV50 export should preserve the canonical telemetry-only token order and still expose node card accessor');
assert(/const\s+ok\s*=\s*await\s+syncTelemetryBackendOnly\s*\(\s*\)/.test(html), 'runtime ticks must continue using canonical syncTelemetryBackendOnly()');

console.log('node-card-cost-runtime-policy.test passed');
