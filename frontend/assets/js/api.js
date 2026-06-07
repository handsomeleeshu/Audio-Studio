export class AudioStudioApi {
  constructor(baseUrl = '') { this.baseUrl = baseUrl; }

  async getConfig() {
    const candidates = [`${this.baseUrl}/api/config`, `${this.baseUrl}/config/A2.json`, `./config/A2.json`, `../config/A2.json`];
    let lastErr;
    for (const url of candidates) {
      try {
        const r = await fetch(url, { cache: 'no-store' });
        if (r.ok) return await r.json();
      } catch (e) { lastErr = e; }
    }
    throw lastErr || new Error('Failed to load config');
  }

  async validatePipeline(pipelineJson) {
    return this.post('/api/pipeline/validate', pipelineJson, { fallback: { ok: true, warnings: [], errors: [] } });
  }

  async buildPipeline(pipelineJson) {
    return this.post('/api/pipeline/build', pipelineJson, { fallback: { ok: true, session_id: 'frontend_mock', core_map: {}, message: 'Frontend fallback build success' } });
  }

  async pipelineEdit(payload) {
    return this.post('/api/pipeline/edit', payload, { fallback: { ok: true, callback: 'frontend_fallback_pipeline_edit', ...payload } });
  }

  async pipelineTool(payload) {
    return this.post('/api/pipeline/tool', payload, { fallback: { ok: true, callback: 'frontend_fallback_pipeline_tool', ...payload } });
  }

  async startRuntime(sessionId) {
    return this.post('/api/runtime/run', { session_id: sessionId }, { fallback: { ok: true, running: true } });
  }

  async stopRuntime(sessionId) {
    return this.post('/api/runtime/stop', { session_id: sessionId }, { fallback: { ok: true, running: false } });
  }

  async updateParam(payload) {
    return this.post('/api/param/update', payload, { fallback: { ok: true, value: payload.value } });
  }

  async nodeAction(payload) {
    return this.post('/api/node/action', payload, { fallback: { ok: true, node_id: payload.node_id, action: payload.action } });
  }

  async getTelemetry(nodes) {
    const ids = (nodes || []).map(n => n.id).join(',');
    try {
      const r = await fetch(`${this.baseUrl}/api/telemetry?nodes=${encodeURIComponent(ids)}`, { cache: 'no-store' });
      if (r.ok) return await r.json();
    } catch (_) {}
    return null;
  }

  async post(path, body, { fallback } = {}) {
    console.info('[AudioStudio Frontend → Backend]', path, body);
    try {
      const r = await fetch(`${this.baseUrl}${path}`, {
        method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body)
      });
      if (r.ok) return await r.json();
    } catch (_) {}
    return fallback;
  }
}
