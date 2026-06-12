#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

find "$ROOT/tests/frontend" -maxdepth 1 -type f -name '*.test.mjs' | sort | while IFS= read -r test_file; do
  echo "[frontend test] ${test_file#$ROOT/}"
  node "$test_file"
done

echo "[test] tests/dev-environment.test.mjs"
node "$ROOT/tests/dev-environment.test.mjs"

"$ROOT/scripts/build_backend.sh"
ctest --test-dir "$ROOT/build" --output-on-failure
