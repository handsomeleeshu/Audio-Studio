#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

FAILED=0
find "$ROOT/tests/frontend" -maxdepth 1 -type f -name '*.test.mjs' | sort | while IFS= read -r test_file; do
  rel="${test_file#$ROOT/}"
  echo "::group::frontend test: $rel"
  if node "$test_file"; then
    echo "::endgroup::"
  else
    rc=$?
    echo "::error title=Frontend test failed::$rel exited with $rc"
    echo "::endgroup::"
    exit "$rc"
  fi
done

echo "::group::test: tests/dev-environment.test.mjs"
node "$ROOT/tests/dev-environment.test.mjs"
echo "::endgroup::"

echo "::group::build backend"
"$ROOT/scripts/build_backend.sh"
echo "::endgroup::"

echo "::group::ctest backend"
ctest --test-dir "$ROOT/build" --output-on-failure
echo "::endgroup::"
