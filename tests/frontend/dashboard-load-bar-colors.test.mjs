import assert from 'node:assert/strict';
import fs from 'node:fs';

const html = fs.readFileSync('frontend/index.html', 'utf8');

assert(html.includes('Dynamic runtime load color mapping for DSP Core and System Health'), 'load color mapping block must be installed');
assert(html.includes('#corePanel .loadbar i') && html.includes('#healthPanel .health-progress i'), 'DSP Core and System Health bar selectors must both be styled');
assert(html.includes('--load-gradient') && html.includes('--load-glow'), 'bar color must be driven by dynamic CSS variables');
assert(html.includes('function loadColorModelV96(percent)'), 'shared percentage-to-color mapping helper must exist');
assert(html.includes('if (p >= 85)') && html.includes('if (p >= 70)') && html.includes('if (p >= 50)'), 'color mapping must use severity thresholds');
assert(html.includes('function colorizeDspCoreLoadingBarsV96()'), 'DSP Core Loading bars must be colorized after render');
assert(html.includes('function colorizeSystemHealthBarsV96()'), 'System Health bars must be colorized after render');
assert(html.includes('__renderCoreLoadingBeforeLoadColorsV96') && html.includes('__renderHealthBeforeLoadColorsV96'), 'final render functions must be wrapped, not only CSS-patched');
assert(!/V\d+Installed/.test(html), 'must not introduce versioned installed guards');

const cssIdx = html.indexOf('Dynamic runtime load color mapping for DSP Core and System Health');
const baseLoadbarIdx = html.indexOf('.loadbar i');
const baseHealthIdx = html.indexOf('.health-progress i');
assert(cssIdx > baseLoadbarIdx && cssIdx > baseHealthIdx, 'dynamic override CSS must appear after base fixed-gradient bar CSS');

console.log('dashboard-load-bar-colors.test passed');
