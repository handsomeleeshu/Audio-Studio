import assert from 'node:assert/strict';
import fs from 'node:fs';
import { convertPipeline } from '../../frontend/assets/js/configParser.js';
import { autoLayout, checkMinDistance, getPortPosition, edgePath, MIN_X_DISTANCE, NODE_WIDTH } from '../../frontend/assets/js/layout.js';

const cfg = JSON.parse(fs.readFileSync(new URL('../../config/A2.json', import.meta.url), 'utf8'));
const graph = convertPipeline(cfg, 'PLAY_MAIN');
autoLayout(graph.nodes, graph.edges);
assert.ok(checkMinDistance(graph.nodes), 'auto layout must keep minimum distance');
assert.equal(NODE_WIDTH, 128, 'legacy pipeline node width should match uploaded HTML style');

const edge = graph.edges[0];
const from = graph.nodes.find(n => n.id === edge.from.nodeId);
const to = graph.nodes.find(n => n.id === edge.to.nodeId);
const p1 = getPortPosition(from, 'output', edge.from.portName);
const p2 = getPortPosition(to, 'input', edge.to.portName);
assert.ok(p2.x - p1.x >= MIN_X_DISTANCE - 150 || p2.x > p1.x, 'edge should have valid geometry');
assert.ok(edgePath(p1, p2).startsWith('M '));

// Regression: when nodes are very close or reversed, edgePath must not collapse
// into a tiny/hidden Bezier. It should use the visible elbow route.
const closePath = edgePath({ x: 200, y: 100 }, { x: 220, y: 116 });
assert.ok(closePath.includes(' L '), 'close node connection should use visible elbow segments');
assert.ok(closePath.endsWith('220 116'));

console.log('layout.test passed');
