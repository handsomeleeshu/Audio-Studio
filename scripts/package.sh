#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$(dirname "$ROOT")"
zip -qr audio_studio_product.zip "$(basename "$ROOT")" -x "*/build/*"
echo "Created $(dirname "$ROOT")/audio_studio_product.zip"
