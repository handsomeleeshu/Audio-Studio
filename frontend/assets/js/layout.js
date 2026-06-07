export const NODE_WIDTH = 218;
export const NODE_BASE_HEIGHT = 106;
export const MIN_X_DISTANCE = 372;
export const MIN_Y_DISTANCE = 176;
export const WORLD_WIDTH = 5200;
export const WORLD_HEIGHT = 2800;

function nodeHeight(node) {
  const portCount = Math.max(node.inputs?.length || 0, node.outputs?.length || 0);
  return Math.max(NODE_BASE_HEIGHT, 82 + portCount * 26);
}

export function getNodeSize(node) {
  return { w: NODE_WIDTH, h: nodeHeight(node) };
}

export function autoLayout(nodes, edges) {
  const byId = new Map(nodes.map(n => [n.id, n]));
  const indeg = new Map(nodes.map(n => [n.id, 0]));
  const out = new Map(nodes.map(n => [n.id, []]));
  for (const e of edges) {
    if (!byId.has(e.from.nodeId) || !byId.has(e.to.nodeId)) continue;
    indeg.set(e.to.nodeId, (indeg.get(e.to.nodeId) || 0) + 1);
    out.get(e.from.nodeId)?.push(e.to.nodeId);
  }
  const queue = [];
  for (const n of nodes) if ((indeg.get(n.id) || 0) === 0) queue.push(n.id);
  const layer = new Map(nodes.map(n => [n.id, 0]));
  const seen = new Set();
  while (queue.length) {
    const id = queue.shift();
    seen.add(id);
    for (const next of out.get(id) || []) {
      layer.set(next, Math.max(layer.get(next) || 0, (layer.get(id) || 0) + 1));
      indeg.set(next, (indeg.get(next) || 0) - 1);
      if (indeg.get(next) === 0) queue.push(next);
    }
  }
  let maxLayer = Math.max(0, ...Array.from(layer.values()));
  for (const n of nodes) {
    if (!seen.has(n.id) && edges.length > 0) {
      maxLayer += 1;
      layer.set(n.id, maxLayer);
    }
  }
  const groups = new Map();
  for (const n of nodes) {
    const l = layer.get(n.id) || 0;
    if (!groups.has(l)) groups.set(l, []);
    groups.get(l).push(n);
  }
  const sortedLayers = Array.from(groups.keys()).sort((a, b) => a - b);
  for (const l of sortedLayers) {
    const arr = groups.get(l).sort((a, b) => {
      const ca = centerHint(a, edges, byId);
      const cb = centerHint(b, edges, byId);
      if (ca !== cb) return ca - cb;
      return a.id.localeCompare(b.id);
    });
    arr.forEach((node, idx) => {
      node.x = 120 + l * MIN_X_DISTANCE;
      node.y = 110 + idx * MIN_Y_DISTANCE;
    });
  }
  spreadOverlaps(nodes);
  return nodes;
}

function centerHint(node, edges, byId) {
  const related = [];
  for (const e of edges) {
    if (e.to.nodeId === node.id && byId.has(e.from.nodeId)) related.push(byId.get(e.from.nodeId).y || 0);
    if (e.from.nodeId === node.id && byId.has(e.to.nodeId)) related.push(byId.get(e.to.nodeId).y || 0);
  }
  if (!related.length) return node.y || 0;
  return related.reduce((a, b) => a + b, 0) / related.length;
}

function spreadOverlaps(nodes) {
  const sorted = [...nodes].sort((a, b) => (a.x - b.x) || (a.y - b.y));
  for (let i = 0; i < sorted.length; i++) {
    for (let j = i + 1; j < sorted.length; j++) {
      const a = sorted[i];
      const b = sorted[j];
      if (Math.abs(a.x - b.x) < MIN_X_DISTANCE * 0.42) {
        const ah = getNodeSize(a).h;
        if (Math.abs(a.y - b.y) < Math.max(MIN_Y_DISTANCE * 0.55, ah * 0.7)) {
          b.y = a.y + MIN_Y_DISTANCE;
        }
      }
    }
  }
}

export function checkMinDistance(nodes, minX = 150, minY = 78) {
  for (let i = 0; i < nodes.length; i++) {
    for (let j = i + 1; j < nodes.length; j++) {
      const a = nodes[i];
      const b = nodes[j];
      const dx = Math.abs(a.x - b.x);
      const dy = Math.abs(a.y - b.y);
      if (dx < minX && dy < minY) return false;
    }
  }
  return true;
}

export function getPortPosition(node, direction, portName) {
  const { h } = getNodeSize(node);
  const ports = direction === 'input' ? node.inputs : node.outputs;
  const index = Math.max(0, ports.findIndex(p => p.name === portName || p.id === portName));
  const count = Math.max(ports.length, 1);
  const step = Math.min(28, Math.max(22, (h - 62) / Math.max(count, 1)));
  const y = node.y + 52 + index * step;
  const x = direction === 'input' ? node.x - 9 : node.x + NODE_WIDTH + 9;
  return { x, y };
}

export function edgePath(fromPos, toPos) {
  const gap = Math.max(74, Math.abs(toPos.x - fromPos.x) * 0.35);
  const sx = fromPos.x;
  const sy = fromPos.y;
  const tx = toPos.x;
  const ty = toPos.y;
  const c1x = sx + gap;
  const c2x = tx - gap;
  return `M ${sx} ${sy} C ${c1x} ${sy}, ${c2x} ${ty}, ${tx} ${ty}`;
}

export function viewportToWorld(clientX, clientY, viewportRect, camera) {
  return {
    x: (clientX - viewportRect.left - camera.x) / camera.zoom,
    y: (clientY - viewportRect.top - camera.y) / camera.zoom
  };
}

export function worldToScreen(x, y, camera) {
  return { x: x * camera.zoom + camera.x, y: y * camera.zoom + camera.y };
}
