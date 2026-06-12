#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

mapfile -t frontend_tests < <(find tests/frontend -maxdepth 1 -type f -name '*.test.mjs' | sort)

for test_file in "${frontend_tests[@]}"; do
  node "$test_file"
done

node tests/dev-environment.test.mjs

"$ROOT/scripts/build_backend.sh"
ctest --test-dir "$ROOT/build" --output-on-failure
