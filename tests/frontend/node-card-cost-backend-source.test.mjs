import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const html = readFileSync('frontend/index.html', 'utf8');

assert(html.includes('function nodeCardRuntimeCostV98'), 'node card backend-cost helper must be installed');
assert(html.includes('function renderNodeCardRuntimeCostMarkupV98'), 'node card runtime-cost markup helper must be installed');
assert(html.includes('function refreshNodeCardRuntimeCostV98'), 'node card runtime-cost refresher must be installed');
assert(html.includes('renderNodeCardRuntimeCostMarkupV98(n)'), 'renderNodes must render CPU/LAT/MEM/Core through backend cost helper');
assert(html.includes('nodeCardCostForV98'), 'PER-ALGORITHM COST API must expose node cost data for node cards');
assert(html.includes('window.audioStudioNodeCardAlgorithmCostV98'), 'node card cost synchronization wrapper must be exposed for debugging');

const renderNodesBlock = html.match(/function renderNodes\(\) \{[\s\S]*?function clearActivePorts\(\)/)?.[0] || '';
assert(renderNodesBlock.includes('renderNodeCardRuntimeCostMarkupV98(n)'), 'renderNodes block must use backend-owned node card cost markup');
assert(!renderNodesBlock.includes('n.cpu.toFixed'), 'node card must not render CPU from n.cpu fallback');
assert(!renderNodesBlock.includes('n.lat.toFixed'), 'node card must not render LAT from n.lat fallback');
assert(!renderNodesBlock.includes('${n.mem} KB'), 'node card must not render MEM from n.mem fallback');
assert(!renderNodesBlock.includes('Core ${n.core}'), 'node card must not render Core from n.core fallback');

assert(!html.includes('Number(n.core ?? item?.core ?? 0)'), 'Per-Algorithm Cost Core column must not prefer frontend n.core over backend item.core');
assert(!html.includes('raw.core ?? n?.core ?? 0'), 'normalized cost item must not fallback to frontend n.core for backend-owned core');
assert(html.includes("core: raw.core === null || raw.core === undefined"), 'cost item core must be null when backend omits core');
assert(html.includes("core: Number.isFinite(core) ? `Core ${Math.round(core)}` : 'Core N/A'"), 'node card must show Core N/A when backend omits core');
assert(html.includes("if (!running || !item) return { cpu: 'N/A'"), 'node card must show N/A when backend cost item is absent');

console.log('node-card-cost-backend-source.test passed');
