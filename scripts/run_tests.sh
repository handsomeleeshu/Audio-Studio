#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

frontend_tests=(
  tests/frontend/config-parser.test.mjs
  tests/frontend/plain-html-integration.test.mjs
  tests/frontend/standalone-features.test.mjs
  tests/frontend/performance-profile.test.mjs
  tests/frontend/dead-code-policy.test.mjs
  tests/frontend/runtime-loop-policy.test.mjs
  tests/frontend/cost-table-data-total-and-selection.test.mjs
  tests/frontend/cost-total-startup-safe-v103.test.mjs
  tests/frontend/cost-total-startup-safe.test.mjs
  tests/frontend/pipeline-selection-buffer-edge.test.mjs
)

for test_file in "${frontend_tests[@]}"; do
  if [[ ! -f "$test_file" ]]; then
    echo "[frontend test skipped] $test_file"
    continue
  fi
  echo "[frontend test] $test_file"
  node "$test_file"
done

echo "[test] tests/dev-environment.test.mjs"
node "$ROOT/tests/dev-environment.test.mjs"

"$ROOT/scripts/build_backend.sh"
ctest --test-dir "$ROOT/build" --output-on-failure
