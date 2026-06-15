import assert from 'assert';
import { connectUnique, validateConnection, endpointKey } from '../../GUI/frontend/assets/js/pipelineRules.js';

const nodes = [
  { id: 'A', outputs: [{ name: 'out' }], inputs: [] },
  { id: 'B', inputs: [{ name: 'in' }], outputs: [{ name: 'out' }] },
  { id: 'C', inputs: [{ name: 'in' }], outputs: [] }
];

let edges = [];
let r = validateConnection(nodes, edges, { nodeId: 'A', portName: 'out' }, { nodeId: 'B', portName: 'in' });
assert.equal(r.ok, true);
edges = connectUnique(edges, { nodeId: 'A', portName: 'out' }, { nodeId: 'B', portName: 'in' });
assert.equal(edges.length, 1);

// A:out can only connect to one input. New connection replaces old one.
edges = connectUnique(edges, { nodeId: 'A', portName: 'out' }, { nodeId: 'C', portName: 'in' });
assert.equal(edges.length, 1);
assert.equal(endpointKey(edges[0].to), 'C:in');

// One input can only have one driving output as well.
edges = connectUnique(edges, { nodeId: 'B', portName: 'out' }, { nodeId: 'C', portName: 'in' });
assert.equal(edges.length, 1);
assert.equal(endpointKey(edges[0].from), 'B:out');
assert.equal(endpointKey(edges[0].to), 'C:in');

const invalid = validateConnection(nodes, edges, { nodeId: 'A', portName: 'bad' }, { nodeId: 'C', portName: 'in' });
assert.equal(invalid.ok, false);
console.log('connection-policy.test passed');
