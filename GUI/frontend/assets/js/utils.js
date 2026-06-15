export function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

export function uid(prefix = 'id') {
  return `${prefix}_${Math.random().toString(16).slice(2, 8)}_${Date.now().toString(36)}`;
}

export function deepClone(value) {
  return JSON.parse(JSON.stringify(value));
}

export function safeName(value) {
  return String(value || '').replace(/[^a-zA-Z0-9_\-:.]/g, '_');
}

export function groupBy(items, getter) {
  const out = new Map();
  for (const item of items) {
    const key = getter(item) || 'misc';
    if (!out.has(key)) out.set(key, []);
    out.get(key).push(item);
  }
  return out;
}

export function getByPath(obj, path) {
  if (!path) return undefined;
  const parts = String(path).split('.');
  let cur = obj;
  for (const part of parts) {
    if (cur == null) return undefined;
    cur = cur[part];
  }
  return cur;
}

export function formatNumber(n, digits = 1) {
  if (Number.isNaN(Number(n))) return '-';
  return Number(n).toFixed(digits);
}

export function parseEndpoint(endpoint) {
  const [nodeId, portName] = String(endpoint).split(':');
  return { nodeId, portName: portName || '' };
}

export function nowTime() {
  const d = new Date();
  return d.toLocaleTimeString('en-GB', { hour12: false });
}
