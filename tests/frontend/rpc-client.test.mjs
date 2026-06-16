import { strict as assert } from 'assert';
import fs from 'fs';

const html = fs.readFileSync(new URL('../../GUI/frontend/index.html', import.meta.url), 'utf8');
const backend = fs.readFileSync(new URL('../../GUI/backend/src/http_server.cpp', import.meta.url), 'utf8');

assert.ok(html.includes('async function jsonRpcCall'), 'frontend should expose a JSON-RPC call helper');
assert.ok(html.includes("endpoint = '/rpc'"), 'frontend JSON-RPC helper should default to POST /rpc');
assert.ok(html.includes('window.AudioStudioJsonRpc'), 'frontend should publish the JSON-RPC helper for UI modules');
assert.ok(backend.includes('req.method == "POST" && req.path == "/rpc"'), 'GUI backend should expose POST /rpc');
assert.ok(backend.includes('JsonRpcEndpoint'), 'GUI backend /rpc should use the shared JSON-RPC endpoint');

console.log('rpc-client.test passed');
