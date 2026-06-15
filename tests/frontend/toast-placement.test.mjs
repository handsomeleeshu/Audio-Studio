import { strict as assert } from 'assert';
import { readFileSync } from 'fs';

const html = readFileSync('GUI/frontend/index.html', 'utf8');
const start = html.indexOf('/* Pipeline runtime UX polish. */');
const end = html.indexOf('/* Pipeline-scoped runtime state colors. */', start);
assert(start >= 0 && end > start, 'runtime UX CSS block must exist');
const block = html.slice(start, end);

assert.match(block, /\.toast\s*\{\s*top:\s*156px;/s, 'toast base position should be lowered into pipeline layout');
assert.match(block, /\.toast\.show\s*\{\s*top:\s*166px;/s, 'visible toast position should be lowered into pipeline layout');
assert.doesNotMatch(block, /\.toast\s*\{\s*top:\s*116px;/s, 'old high toast base position must be removed');
assert.doesNotMatch(block, /\.toast\.show\s*\{\s*top:\s*126px;/s, 'old high visible toast position must be removed');

console.log('toast-placement.test passed');
