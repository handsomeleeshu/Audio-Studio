from pathlib import Path

INDEX = Path('frontend/index.html')
RUNNER = Path('scripts/run_tests.sh')
TEST = Path('tests/frontend/pipeline-selection-buffer-edge.test.mjs')

html = INDEX.read_text(encoding='utf-8')

def replace_once(src: str, old: str, new: str, label: str) -> str:
    count = src.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected 1 match, found {count}')
    return src.replace(old, new, 1)

def insert_before_once(src: str, marker: str, insert: str, label: str) -> str:
    count = src.count(marker)
    if count != 1:
        raise SystemExit(f'{label}: expected 1 marker, found {count}')
    return src.replace(marker, insert + marker, 1)

css_marker = '    /* Dynamic runtime load color mapping for DSP Core and System Health. */'
css_insert = '''\n\n    /* Buffer edge runtime labels and stable flow particles. */\n    .edge-label {\n      fill: #d9fbff;\n      font-size: 11px;\n      font-weight: 900;\n      letter-spacing: .03em;\n      text-anchor: middle;\n      dominant-baseline: central;\n      paint-order: stroke;\n      stroke: #06101c;\n      stroke-width: 4px;\n      stroke-linejoin: round;\n      pointer-events: none;\n      filter: drop-shadow(0 0 8px rgba(0, 216, 255, .38));\n    }\n\n    .edge-particle {\n      fill: var(--green);\n      stroke: #effff4;\n      stroke-width: 1px;\n      pointer-events: none;\n      filter: drop-shadow(0 0 8px rgba(49, 242, 107, .9));\n    }\n'''
if 'stable flow particles' not in html:
    html = insert_before_once(html, css_marker, css_insert, 'edge css insert')

old_rate = '''    function edgeSampleRateLabelForEdge(ep, key = '') {\n      const fmt = key ? cachedEdgeRuntimeFormat(key) : null;\n      if (!fmt || fmt.source !== 'backend') return '';\n      return formatSampleRateLabel(fmt.sampleRate || fmt.sample_rate);\n    }\n'''
new_rate = '''    function edgeSampleRateLabelForEdge(ep, key = '') {\n      const fmt = (key ? cachedEdgeRuntimeFormat(key) : null) || inferredEdgeFormat(ep);\n      const rate = Number(fmt?.sampleRate || fmt?.sample_rate || 48000);\n      return formatSampleRateLabel(Number.isFinite(rate) && rate > 0 ? rate : 48000);\n    }\n'''
html = replace_once(html, old_rate, new_rate, 'edge sample-rate fallback')

old_selection = '''    function renderSelectionChange({ redrawEdges = true, inspector = true, resetInspector = false } = {}) {\n      $$('.pipeline-node').forEach(el => el.classList.toggle('selected', isNodeSelected(el.dataset.id)));\n      if (resetInspector) inspectorRenderKey = '';\n      if (redrawEdges) drawEdges();\n      if (inspector) renderInspector();\n    }\n'''
new_selection = old_selection + '''    function shouldHandlePipelineSelectAllV104(e) {\n      if (!e || !(e.ctrlKey || e.metaKey) || String(e.key || '').toLowerCase() !== 'a') return false;\n      const target = e.target;\n      const tag = String(target?.tagName || '').toUpperCase();\n      if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT' || target?.isContentEditable) return false;\n      return !!document.getElementById('canvasWrap');\n    }\n    function selectAllVisiblePipelineLayoutV104() {\n      selectedNodeIds = new Set(state.nodes.map(n => n.id));\n      selectedEdgeKeys = new Set(state.edges.map(edgeKeyForEdge));\n      selectedNodeId = state.nodes[0]?.id || '';\n      selectedEdgeKey = selectedEdgeKeys.values().next().value || '';\n      inspectorActiveTab = 'general';\n      inspectorRenderKey = '';\n      inspectorBackendContextKey = '';\n      renderSelectionChange({ resetInspector: true });\n      drawEdges();\n      addLog('info', 'Select all visible layout', `${selectedNodeIds.size} node(s), ${selectedEdgeKeys.size} connection(s)`);\n      backendEdit('selection_all_visible_layout', { pipeline_id: currentPipelineId, nodes: [...selectedNodeIds], edges: [...selectedEdgeKeys] });\n      toast(`Selected ${selectedNodeIds.size} node(s), ${selectedEdgeKeys.size} connection(s)`);\n      return true;\n    }\n'''
html = replace_once(html, old_selection, new_selection, 'ctrl+a selection helper')

old_draw_helpers_marker = '''    function drawEdges() {\n      syncWorldGeometry();\n'''
new_draw_helpers = '''    function stableEdgeHashV104(text = '') {\n      let h = 2166136261;\n      String(text || '').split('').forEach(ch => { h ^= ch.charCodeAt(0); h = Math.imul(h, 16777619); });\n      return h >>> 0;\n    }\n    function edgeParticleDurationV104(key = '') {\n      return 2.15 + (stableEdgeHashV104(key) % 7) * 0.08;\n    }\n    function edgeParticleBeginV104(key = '', duration = 2.4) {\n      const now = ((window.performance && performance.now) ? performance.now() : Date.now()) / 1000;\n      const seed = (stableEdgeHashV104(key) % 1000) / 1000;\n      const phase = (now + seed * duration) % duration;\n      return `-${phase.toFixed(3)}s`;\n    }\n    function appendEdgeRuntimeParticleV104(svg, key, d) {\n      const duration = edgeParticleDurationV104(key);\n      const dot = document.createElementNS('http://www.w3.org/2000/svg', 'circle');\n      dot.setAttribute('r', '3.2');\n      dot.setAttribute('class', 'edge-particle');\n      const anim = document.createElementNS('http://www.w3.org/2000/svg', 'animateMotion');\n      anim.setAttribute('dur', `${duration.toFixed(2)}s`);\n      anim.setAttribute('repeatCount', 'indefinite');\n      anim.setAttribute('path', d);\n      anim.setAttribute('begin', edgeParticleBeginV104(key, duration));\n      dot.appendChild(anim);\n      svg.appendChild(dot);\n    }\n    function appendEdgeSampleRateLabelV104(svg, a, b, ep, key) {\n      const label = edgeSampleRateLabelForEdge(ep, key);\n      if (!label) return false;\n      const pa = portPoint(a, 'out', ep.fromPort), pb = portPoint(b, 'in', ep.toPort);\n      const tx = (pa.x + pb.x) / 2, ty = (pa.y + pb.y) / 2 - 8;\n      const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');\n      text.setAttribute('x', tx);\n      text.setAttribute('y', ty);\n      text.setAttribute('class', 'edge-label');\n      text.textContent = label;\n      svg.appendChild(text);\n      return true;\n    }\n    function drawEdges() {\n      syncWorldGeometry();\n'''
html = replace_once(html, old_draw_helpers_marker, new_draw_helpers, 'edge helper insert')

old_edge_runtime = '''        if (running) {\n          const dot = document.createElementNS('http://www.w3.org/2000/svg', 'circle'); dot.setAttribute('r', '3'); dot.setAttribute('class', 'edge-particle');\n          const anim = document.createElementNS('http://www.w3.org/2000/svg', 'animateMotion'); anim.setAttribute('dur', `${2.1 + rnd(0, .7)}s`); anim.setAttribute('repeatCount', 'indefinite'); anim.setAttribute('path', d); anim.setAttribute('begin', `${idx * .12}s`); dot.appendChild(anim); svg.appendChild(dot);\n        }\n        if (edgePipelineRuntimeState(ep) === 'running') { const label = edgeSampleRateLabelForEdge(ep, key); if (label) { const pa = portPoint(a, 'out', ep.fromPort), pb = portPoint(b, 'in', ep.toPort); const tx = (pa.x + pb.x) / 2, ty = (pa.y + pb.y) / 2 - 8; const text = document.createElementNS('http://www.w3.org/2000/svg', 'text'); text.setAttribute('x', tx); text.setAttribute('y', ty); text.setAttribute('class', 'edge-label'); text.textContent = label; svg.appendChild(text); } }\n'''
new_edge_runtime = '''        const edgeRuntimeStateV104 = edgePipelineRuntimeState(ep);\n        const edgeRunningV104 = edgeRuntimeStateV104 === 'running';\n        if (edgeRunningV104) appendEdgeRuntimeParticleV104(svg, key, d);\n        if (edgeRunningV104) appendEdgeSampleRateLabelV104(svg, a, b, ep, key);\n'''
html = replace_once(html, old_edge_runtime, new_edge_runtime, 'edge particle and label render')

keyboard_marker = '''    window.addEventListener('resize', () => { updateWorldBounds(); if (!defaultViewportZoomApplied) applyInitialViewportZoomIfNeeded('resize_before_first_view'); renderAll(true); });\n'''
keyboard_insert = '''    document.addEventListener('keydown', e => {\n      if (!shouldHandlePipelineSelectAllV104(e)) return;\n      e.preventDefault();\n      e.stopPropagation();\n      selectAllVisiblePipelineLayoutV104();\n    }, true);\n\n'''
if 'selection_all_visible_layout' not in html.split(keyboard_marker)[0]:
    html = insert_before_once(html, keyboard_marker, keyboard_insert, 'ctrl+a listener insert')

INDEX.write_text(html, encoding='utf-8')

TEST.write_text("""import fs from 'node:fs';\nimport assert from 'node:assert/strict';\n\nconst html = fs.readFileSync(new URL('../../frontend/index.html', import.meta.url), 'utf8');\n\nassert.ok(html.includes('function shouldHandlePipelineSelectAllV104'), 'Ctrl+A guard should exist');\nassert.ok(html.includes('function selectAllVisiblePipelineLayoutV104'), 'visible layout select-all helper should exist');\nassert.ok(html.includes('selection_all_visible_layout'), 'select-all should report backend edit telemetry');\nassert.ok(html.includes("selectedNodeIds = new Set(state.nodes.map(n => n.id))"), 'Ctrl+A should select all visible nodes');\nassert.ok(html.includes('selectedEdgeKeys = new Set(state.edges.map(edgeKeyForEdge))'), 'Ctrl+A should select all visible edges');\n\nassert.ok(html.includes('function edgeParticleBeginV104'), 'stable particle begin helper should exist');\nassert.ok(html.includes('appendEdgeRuntimeParticleV104(svg, key, d)'), 'drawEdges should use stable particle helper');\nassert.ok(!html.includes('2.1 + rnd(0, .7)'), 'edge particle duration should not be random per redraw');\nassert.ok(html.includes('appendEdgeSampleRateLabelV104(svg, a, b, ep, key)'), 'running edge sample-rate labels should be rendered');\nassert.ok(/function edgeSampleRateLabelForEdge[\\s\\S]*inferredEdgeFormat\\(ep\\)/.test(html), 'sample-rate label should fall back to inferred format when backend format is not cached');\nassert.ok(/const edgeRunningV104 = edgeRuntimeStateV104 === 'running'/.test(html), 'edge particles should follow pipeline-scoped runtime state');\n\nconsole.log('pipeline-selection-buffer-edge.test passed');\n""", encoding='utf-8')

runner = RUNNER.read_text(encoding='utf-8')
new_test = '  tests/frontend/pipeline-selection-buffer-edge.test.mjs\n'
if new_test not in runner:
    runner = runner.replace('  tests/frontend/cost-total-startup-safe.test.mjs\n)', '  tests/frontend/cost-total-startup-safe.test.mjs\n' + new_test + ')')
RUNNER.write_text(runner, encoding='utf-8')
print('pipeline edge patch applied')
