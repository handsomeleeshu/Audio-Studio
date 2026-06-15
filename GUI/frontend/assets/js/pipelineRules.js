export function endpointKey(ep) {
  return `${ep && ep.nodeId || ''}:${ep && ep.portName || ''}`;
}

export function findPort(nodes, endpoint, direction) {
  const node = nodes.find(n => n.id === endpoint.nodeId);
  if (!node) return null;
  const ports = direction === 'output' ? node.outputs : node.inputs;
  return ports.find(p => p.name === endpoint.portName || p.id === endpoint.portName) || null;
}

export function validateConnection(nodes, edges, from, to) {
  if (!from || !to) return { ok: false, reason: 'Missing endpoint' };
  if (from.nodeId === to.nodeId) return { ok: false, reason: 'Self connection is not allowed' };
  if (!findPort(nodes, from, 'output')) return { ok: false, reason: `Output port not found: ${endpointKey(from)}` };
  if (!findPort(nodes, to, 'input')) return { ok: false, reason: `Input port not found: ${endpointKey(to)}` };
  const fromKey = endpointKey(from);
  const toKey = endpointKey(to);
  const duplicate = edges.some(e => endpointKey(e.from) === fromKey && endpointKey(e.to) === toKey);
  if (duplicate) return { ok: false, reason: 'Connection already exists' };
  return { ok: true, reason: '' };
}

export function connectUnique(edges, from, to) {
  const fromKey = endpointKey(from);
  const toKey = endpointKey(to);
  const filtered = edges.filter(e => endpointKey(e.from) !== fromKey && endpointKey(e.to) !== toKey);
  return [...filtered, {
    id: `edge_${from.nodeId}_${from.portName}_${to.nodeId}_${to.portName}_${Date.now().toString(36)}`,
    from: { nodeId: from.nodeId, portName: from.portName },
    to: { nodeId: to.nodeId, portName: to.portName }
  }];
}
