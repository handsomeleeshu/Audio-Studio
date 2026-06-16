import fs from 'fs';
import os from 'os';
import path from 'path';
import { spawn } from 'child_process';

const root = path.resolve(new URL('../../', import.meta.url).pathname);
const profileDir = path.join(root, 'profiles', 'frontend');
const profileDirLabel = 'profiles/frontend';
const defaultChromePath = process.platform === 'darwin'
  ? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
  : 'google-chrome';

const options = {
  appPort: Number(process.env.AUDIO_STUDIO_PROFILE_PORT || 18080),
  cdpPort: Number(process.env.AUDIO_STUDIO_CHROME_DEBUG_PORT || 9223),
  durationMs: Number(process.env.AUDIO_STUDIO_PROFILE_MS || 12000),
  warmupMs: Number(process.env.AUDIO_STUDIO_PROFILE_WARMUP_MS || 1500),
  scenario: process.env.AUDIO_STUDIO_PROFILE_SCENARIO || 'idle',
  url: process.env.AUDIO_STUDIO_PROFILE_URL || '',
  chromePath: process.env.CHROME_BIN || process.env.AUDIO_STUDIO_CHROME || defaultChromePath,
  keepBrowser: process.env.AUDIO_STUDIO_PROFILE_KEEP_BROWSER === '1',
  verbose: process.env.AUDIO_STUDIO_PROFILE_VERBOSE === '1'
};

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

async function waitFor(check, label, timeoutMs = 15000) {
  const deadline = Date.now() + timeoutMs;
  let lastError;
  while (Date.now() < deadline) {
    try {
      const value = await check();
      if (value) return value;
    } catch (error) {
      lastError = error;
    }
    await sleep(150);
  }
  throw new Error(`${label} did not become ready${lastError ? `: ${lastError.message}` : ''}`);
}

function run(command, args, { cwd = root, env = process.env } = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { cwd, env, stdio: 'inherit' });
    child.on('error', reject);
    child.on('exit', code => {
      if (code === 0) resolve();
      else reject(new Error(`${command} ${args.join(' ')} exited with ${code}`));
    });
  });
}

async function ensureBackend() {
  if (options.url) return { url: options.url, stop: () => { } };

  const server = path.join(root, 'out', 'linux', 'a2', 'gui_backend', 'Release', 'audio_studio_server');
  if (!fs.existsSync(server)) {
    await run(path.join(root, 'scripts', 'build_all.sh'), ['--profile', 'gui_backend', '-r', 'linux', 'a2']);
  }

  const child = spawn(server, [root, String(options.appPort)], {
    cwd: root,
    stdio: ['ignore', 'pipe', 'pipe']
  });
  if (options.verbose) {
    child.stdout.on('data', chunk => process.stdout.write(chunk));
    child.stderr.on('data', chunk => process.stderr.write(chunk));
  }

  const url = `http://127.0.0.1:${options.appPort}`;
  await waitFor(async () => {
    const response = await fetch(`${url}/api/projects`);
    return response.ok;
  }, 'Audio Studio backend');

  return {
    url,
    stop: () => {
      child.kill('SIGTERM');
    }
  };
}

async function launchChrome() {
  const userDataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'audio-studio-profile-chrome-'));
  const args = [
    '--headless=new',
    '--disable-gpu',
    '--no-first-run',
    '--no-default-browser-check',
    '--disable-background-networking',
    '--disable-extensions',
    '--enable-precise-memory-info',
    '--window-size=1440,1000',
    `--remote-debugging-port=${options.cdpPort}`,
    `--user-data-dir=${userDataDir}`
  ];
  const child = spawn(options.chromePath, args, { stdio: ['ignore', 'ignore', 'pipe'] });
  if (options.verbose) child.stderr.on('data', chunk => process.stderr.write(chunk));

  const version = await waitFor(async () => {
    const response = await fetch(`http://127.0.0.1:${options.cdpPort}/json/version`);
    if (!response.ok) return null;
    return response.json();
  }, 'Chrome DevTools Protocol');

  return {
    version,
    stop: async () => {
      if (options.keepBrowser) return;
      if (!child.killed) child.kill('SIGTERM');
      await Promise.race([
        new Promise(resolve => child.once('exit', resolve)),
        sleep(1200)
      ]);
      try {
        fs.rmSync(userDataDir, { recursive: true, force: true, maxRetries: 3, retryDelay: 100 });
      } catch {
        // Chrome can release profile files just after process exit; stale temp dirs
        // must not make an otherwise successful profile run fail.
      }
    }
  };
}

class CdpClient {
  constructor(webSocketUrl) {
    this.webSocketUrl = webSocketUrl;
    this.nextId = 1;
    this.pending = new Map();
    this.listeners = new Map();
  }

  async connect() {
    this.ws = new WebSocket(this.webSocketUrl);
    await new Promise((resolve, reject) => {
      this.ws.addEventListener('open', resolve, { once: true });
      this.ws.addEventListener('error', reject, { once: true });
    });
    this.ws.addEventListener('message', event => this.onMessage(event));
  }

  onMessage(event) {
    const msg = JSON.parse(event.data);
    if (msg.id && this.pending.has(msg.id)) {
      const { resolve, reject } = this.pending.get(msg.id);
      this.pending.delete(msg.id);
      if (msg.error) reject(new Error(`${msg.error.message || 'CDP error'} (${msg.error.code || 'unknown'})`));
      else resolve(msg.result || {});
      return;
    }
    const key = `${msg.sessionId || ''}:${msg.method}`;
    for (const listener of this.listeners.get(key) || []) listener(msg.params || {});
  }

  send(method, params = {}, sessionId = undefined) {
    const id = this.nextId++;
    const message = { id, method, params };
    if (sessionId) message.sessionId = sessionId;
    this.ws.send(JSON.stringify(message));
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
    });
  }

  once(method, sessionId = undefined) {
    const key = `${sessionId || ''}:${method}`;
    return new Promise(resolve => {
      const list = this.listeners.get(key) || [];
      list.push(resolve);
      this.listeners.set(key, list);
    }).finally(() => {
      const list = this.listeners.get(key) || [];
      this.listeners.set(key, list.slice(1));
    });
  }

  close() {
    if (this.ws) this.ws.close();
  }
}

function profileProbeSource() {
  return String.raw`
(() => {
  if (window.__audioStudioProfileProbeInstalled) return;
  window.__audioStudioProfileProbeInstalled = true;
  const nativeFetch = window.fetch.bind(window);
  const nativeSetInterval = window.setInterval.bind(window);
  const nativeSetTimeout = window.setTimeout.bind(window);
  const nativeRaf = window.requestAnimationFrame.bind(window);
  const data = window.__audioStudioProfile = {
    startedAt: performance.now(),
    longTasks: [],
    frames: [],
    requests: [],
    intervals: [],
    timeouts: [],
    rafScheduled: 0,
    rafCallbacks: 0
  };
  let lastFrame = 0;
  window.__audioStudioProfileReset = () => {
    data.startedAt = performance.now();
    data.longTasks = [];
    data.frames = [];
    data.requests = [];
    data.timeouts = [];
    data.rafScheduled = 0;
    data.rafCallbacks = 0;
    lastFrame = 0;
    return true;
  };
  try {
    new PerformanceObserver(list => {
      for (const entry of list.getEntries()) {
        data.longTasks.push({ start: entry.startTime, duration: entry.duration, name: entry.name || 'longtask' });
      }
    }).observe({ entryTypes: ['longtask'] });
  } catch (_) {}
  window.fetch = async (...args) => {
    const started = performance.now();
    const raw = args[0] && typeof args[0] === 'object' && 'url' in args[0] ? args[0].url : args[0];
    const url = String(raw || '');
    try {
      const response = await nativeFetch(...args);
      data.requests.push({ url, status: response.status, duration: performance.now() - started });
      return response;
    } catch (error) {
      data.requests.push({ url, status: 0, duration: performance.now() - started, error: String(error && error.message || error) });
      throw error;
    }
  };
  window.setInterval = (fn, delay, ...rest) => {
    data.intervals.push({ delay: Number(delay) || 0, stack: String(new Error().stack || '').split('\n').slice(2, 7).join('\n') });
    return nativeSetInterval(fn, delay, ...rest);
  };
  window.setTimeout = (fn, delay, ...rest) => {
    data.timeouts.push({ delay: Number(delay) || 0 });
    return nativeSetTimeout(fn, delay, ...rest);
  };
  window.requestAnimationFrame = callback => {
    data.rafScheduled += 1;
    return nativeRaf(timestamp => {
      data.rafCallbacks += 1;
      return callback(timestamp);
    });
  };
  const frameLoop = timestamp => {
    if (lastFrame) data.frames.push(timestamp - lastFrame);
    lastFrame = timestamp;
    nativeRaf(frameLoop);
  };
  nativeRaf(frameLoop);
  window.__audioStudioProfileSnapshot = () => {
    const frames = data.frames.slice();
    const sortedFrames = frames.slice().sort((a, b) => a - b);
    const percentile = p => sortedFrames.length ? sortedFrames[Math.min(sortedFrames.length - 1, Math.floor(sortedFrames.length * p))] : 0;
    const requestsByPath = {};
    for (const req of data.requests) {
      let key = req.url;
      try { key = new URL(req.url, location.href).pathname; } catch (_) {}
      requestsByPath[key] = (requestsByPath[key] || 0) + 1;
    }
    const longTaskTotalMs = data.longTasks.reduce((sum, item) => sum + item.duration, 0);
    return {
      href: location.href,
      elapsedMs: performance.now() - data.startedAt,
      domNodes: document.getElementsByTagName('*').length,
      heapUsedBytes: performance.memory && performance.memory.usedJSHeapSize || 0,
      heapTotalBytes: performance.memory && performance.memory.totalJSHeapSize || 0,
      longTaskCount: data.longTasks.length,
      longTaskTotalMs,
      maxLongTaskMs: data.longTasks.reduce((max, item) => Math.max(max, item.duration), 0),
      frameCount: frames.length,
      avgFrameMs: frames.length ? frames.reduce((sum, v) => sum + v, 0) / frames.length : 0,
      p95FrameMs: percentile(0.95),
      p99FrameMs: percentile(0.99),
      framesOver50ms: frames.filter(v => v > 50).length,
      requestsTotal: data.requests.length,
      requestsByPath,
      intervals: data.intervals,
      timeoutsScheduled: data.timeouts.length,
      rafScheduled: data.rafScheduled,
      rafCallbacks: data.rafCallbacks
    };
  };
})();
`;
}

function metricsMap(result) {
  return Object.fromEntries((result.metrics || []).map(metric => [metric.name, metric.value]));
}

function deltaMetrics(start, end) {
  const keys = [
    'TaskDuration',
    'ScriptDuration',
    'LayoutDuration',
    'RecalcStyleDuration',
    'LayoutCount',
    'RecalcStyleCount',
    'Nodes',
    'JSHeapUsedSize'
  ];
  return Object.fromEntries(keys.map(key => [key, (end[key] || 0) - (start[key] || 0)]));
}

async function createPage(client, url) {
  const { targetId } = await client.send('Target.createTarget', { url: 'about:blank' });
  const { sessionId } = await client.send('Target.attachToTarget', { targetId, flatten: true });
  await client.send('Page.enable', {}, sessionId);
  await client.send('Runtime.enable', {}, sessionId);
  await client.send('Performance.enable', {}, sessionId);
  await client.send('Page.addScriptToEvaluateOnNewDocument', { source: profileProbeSource() }, sessionId);
  const loaded = client.once('Page.loadEventFired', sessionId);
  await client.send('Page.navigate', { url }, sessionId);
  await loaded;
  return sessionId;
}

async function evaluate(client, sessionId, expression) {
  const result = await client.send('Runtime.evaluate', {
    expression,
    awaitPromise: true,
    returnByValue: true
  }, sessionId);
  if (result.exceptionDetails) {
    throw new Error(result.exceptionDetails.text || 'Runtime.evaluate failed');
  }
  return result.result ? result.result.value : undefined;
}

async function startInteractionDriver(client, sessionId) {
  await evaluate(client, sessionId, String.raw`
(() => {
  window.__audioStudioProfileInteractionStop?.();
  let tickCount = 0;
  const tick = () => {
    tickCount += 1;
    const sc = document.querySelector('#canvasScroll') || document.querySelector('#canvasWrap');
    if (sc) {
      const maxX = Math.max(1, sc.scrollWidth - sc.clientWidth);
      const maxY = Math.max(1, sc.scrollHeight - sc.clientHeight);
      sc.scrollLeft = (tickCount * 97) % maxX;
      sc.scrollTop = (tickCount * 53) % maxY;
      sc.dispatchEvent(new Event('scroll', { bubbles: true }));
    }
    const nodes = Array.from(document.querySelectorAll('.pipeline-node'));
    if (nodes.length && tickCount % 2 === 0) nodes[(tickCount * 7) % nodes.length].click();
  };
  const timer = setInterval(tick, 120);
  window.__audioStudioProfileInteractionStop = () => {
    clearInterval(timer);
    delete window.__audioStudioProfileInteractionStop;
    return true;
  };
  return true;
})()
`);
}

async function stopInteractionDriver(client, sessionId) {
  await evaluate(client, sessionId, 'window.__audioStudioProfileInteractionStop?.() ?? true');
}

async function runScenario(client, baseUrl, scenario) {
  const sessionId = await createPage(client, baseUrl);
  await waitFor(
    () => evaluate(client, sessionId, 'Boolean(document.querySelector("#runBtn") && document.querySelector("#pipelineWorld"))'),
    'Audio Studio UI'
  );

  if (scenario === 'running' || scenario === 'interaction') {
    await evaluate(client, sessionId, 'document.querySelector("#runBtn").click(); true');
    await waitFor(
      () => evaluate(client, sessionId, 'Boolean(window.__audioStudioProfile.requests.some(req => new URL(req.url, location.href).pathname === "/api/runtime/run"))'),
      'runtime run request',
      5000
    );
  }

  await sleep(options.warmupMs);
  await evaluate(client, sessionId, 'window.__audioStudioProfileReset()');
  if (scenario === 'interaction') await startInteractionDriver(client, sessionId);
  const before = metricsMap(await client.send('Performance.getMetrics', {}, sessionId));
  let after, snapshot;
  try {
    await sleep(options.durationMs);
    after = metricsMap(await client.send('Performance.getMetrics', {}, sessionId));
    snapshot = await evaluate(client, sessionId, 'window.__audioStudioProfileSnapshot()');
  } finally {
    if (scenario === 'interaction') await stopInteractionDriver(client, sessionId);
  }

  return {
    scenario,
    durationMs: options.durationMs,
    warmupMs: options.warmupMs,
    cdpMetricsBefore: before,
    cdpMetricsAfter: after,
    cdpMetricDelta: deltaMetrics(before, after),
    page: snapshot
  };
}

function timestamp() {
  return new Date().toISOString().replace(/[-:]/g, '').replace(/\.\d+Z$/, 'Z');
}

function printSummary(report) {
  for (const item of report.scenarios) {
    const d = item.cdpMetricDelta;
    const p = item.page;
    console.log(`\n[${item.scenario}] ${item.durationMs} ms`);
    console.log(`TaskDuration delta: ${(d.TaskDuration * 1000).toFixed(1)} ms`);
    console.log(`ScriptDuration delta: ${(d.ScriptDuration * 1000).toFixed(1)} ms`);
    console.log(`LayoutDuration delta: ${(d.LayoutDuration * 1000).toFixed(1)} ms`);
    console.log(`Long tasks: ${p.longTaskCount} (${p.longTaskTotalMs.toFixed(1)} ms total, max ${p.maxLongTaskMs.toFixed(1)} ms)`);
    console.log(`Frames: avg ${p.avgFrameMs.toFixed(2)} ms, p95 ${p.p95FrameMs.toFixed(2)} ms, >50ms ${p.framesOver50ms}`);
    console.log(`DOM nodes: ${p.domNodes}, heap: ${(p.heapUsedBytes / 1024 / 1024).toFixed(1)} MiB`);
    console.log(`Requests: ${p.requestsTotal} ${JSON.stringify(p.requestsByPath)}`);
    console.log(`Intervals: ${p.intervals.map(i => i.delay).join(', ') || 'none'}`);
  }
}

async function main() {
  fs.mkdirSync(profileDir, { recursive: true });
  const backend = await ensureBackend();
  const chrome = await launchChrome();
  const client = new CdpClient(chrome.version.webSocketDebuggerUrl);
  await client.connect();

  try {
    const scenarios = options.scenario === 'both' ? ['idle', 'running'] : [options.scenario];
    const report = {
      createdAt: new Date().toISOString(),
      url: backend.url,
      options,
      scenarios: []
    };

    for (const scenario of scenarios) {
      report.scenarios.push(await runScenario(client, backend.url, scenario));
    }

    const outPath = path.join(profileDir, `frontend-profile-${timestamp()}-${options.scenario}.json`);
    fs.writeFileSync(outPath, `${JSON.stringify(report, null, 2)}\n`);
    printSummary(report);
    console.log(`\nProfile written to ${profileDirLabel}/${path.basename(outPath)}`);
  } finally {
    client.close();
    await chrome.stop();
    backend.stop();
  }
}

main().catch(error => {
  console.error(error.stack || error.message);
  process.exitCode = 1;
});
