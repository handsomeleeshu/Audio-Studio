#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
node "$ROOT/tests/frontend/config-parser.test.mjs"
node "$ROOT/tests/frontend/layout.test.mjs"
node "$ROOT/tests/frontend/parameter-policy.test.mjs"
node "$ROOT/tests/frontend/connection-policy.test.mjs"
node "$ROOT/tests/frontend/topbar-panel-menu.test.mjs"
node "$ROOT/tests/frontend/pipeline-edit-callbacks.test.mjs"
"$ROOT/scripts/build_backend.sh"
ctest --test-dir "$ROOT/build" --output-on-failure
