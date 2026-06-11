#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
node "$ROOT/tests/frontend/config-parser.test.mjs"
node "$ROOT/tests/frontend/plain-html-integration.test.mjs"
node "$ROOT/tests/frontend/standalone-features.test.mjs"
node "$ROOT/tests/frontend/performance-profile.test.mjs"
node "$ROOT/tests/dev-environment.test.mjs"
"$ROOT/scripts/build_backend.sh"
ctest --test-dir "$ROOT/build" --output-on-failure
