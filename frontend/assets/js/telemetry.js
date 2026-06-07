export function makeMockTelemetry(nodes, coreCount = 4, running = false) {
  const nodeCost = {};
  for (const node of nodes) {
    const base = running ? 1 : 0;
    const core = Number(node.core || 0) % coreCount;
    nodeCost[node.id] = {
      cpu: +(base + Math.random() * (node.kind === 'port' ? 0.4 : 5.8)).toFixed(2),
      memKb: Math.round((node.kind === 'port' ? 24 : 80) + Math.random() * 720),
      latencyMs: +(Math.random() * (node.kind === 'port' ? 0.18 : 2.2)).toFixed(2),
      core,
      rms: +(-42 + Math.random() * 30).toFixed(1),
      peak: +(-18 + Math.random() * 16).toFixed(1)
    };
  }
  const cores = [];
  for (let i = 0; i < coreCount; i++) {
    cores.push({
      id: i,
      load: +(running ? 12 + Math.random() * 68 : Math.random() * 4).toFixed(1),
      temperature: +(38 + Math.random() * (running ? 28 : 4)).toFixed(1),
      powerMw: Math.round((running ? 420 : 80) + Math.random() * (running ? 1100 : 90))
    });
  }
  return {
    timestamp: Date.now(),
    nodeCost,
    cores,
    health: {
      latencyMs: +(running ? 8 + Math.random() * 18 : 0).toFixed(2),
      bufferOccupancy: +(running ? 20 + Math.random() * 60 : 0).toFixed(1),
      throughput: +(running ? 88 + Math.random() * 20 : 0).toFixed(1),
      xruns: 0,
      memoryMb: +(100 + Math.random() * 280).toFixed(1),
      powerW: +((cores.reduce((a, c) => a + c.powerMw, 0)) / 1000).toFixed(2),
      uptimeSec: Math.floor(performance.now() / 1000)
    }
  };
}

export function drawWaveform(canvas, running, accent = '#37d9ff') {
  const ctx = canvas.getContext('2d');
  const w = canvas.width, h = canvas.height;
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = 'rgba(5,12,24,.72)'; ctx.fillRect(0, 0, w, h);
  ctx.strokeStyle = 'rgba(100,170,255,.16)'; ctx.lineWidth = 1;
  for (let x = 0; x < w; x += 40) { ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke(); }
  for (let y = 0; y < h; y += 32) { ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke(); }
  ctx.strokeStyle = running ? accent : 'rgba(139,168,200,.32)'; ctx.lineWidth = 2;
  ctx.beginPath();
  const t = performance.now() / 420;
  for (let x = 0; x < w; x++) {
    const a = running ? (Math.sin(x * 0.035 + t) * 0.38 + Math.sin(x * 0.011 + t * 1.7) * 0.28 + (Math.random() - .5) * 0.06) : 0;
    const y = h / 2 + a * h * 0.38;
    if (x === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  }
  ctx.stroke();
}

export function drawSpectrum(canvas, running) {
  const ctx = canvas.getContext('2d');
  const w = canvas.width, h = canvas.height;
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = 'rgba(5,12,24,.72)'; ctx.fillRect(0, 0, w, h);
  const bars = 48;
  const gap = 3;
  const bw = (w - gap * (bars + 1)) / bars;
  for (let i = 0; i < bars; i++) {
    const n = running ? Math.max(0.05, Math.pow(1 - i / bars, .9) * (0.25 + Math.random() * .75)) : 0.04;
    const bh = n * h * 0.84;
    const x = gap + i * (bw + gap);
    const y = h - bh;
    const g = ctx.createLinearGradient(0, y, 0, h);
    g.addColorStop(0, '#42f59b'); g.addColorStop(.55, '#37d9ff'); g.addColorStop(1, '#448bff');
    ctx.fillStyle = g;
    ctx.fillRect(x, y, bw, bh);
  }
}
