#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${1:-8080}"
DURATION="${AUDIO_STUDIO_PROFILE_SECONDS:-30}"
INTERVAL_MS="${AUDIO_STUDIO_SAMPLE_INTERVAL_MS:-1}"
PROFILER="${AUDIO_STUDIO_PROFILER:-sample}"
BIN="$ROOT/build/profile/audio_studio_gui_server"
OUT_DIR="$ROOT/profiles/backend"
STAMP="$(date +%Y%m%d-%H%M%S)"

if [[ ! -x "$BIN" ]]; then
  cmake --preset profile
  cmake --build --preset profile --parallel
fi

mkdir -p "$OUT_DIR"

if [[ "$PROFILER" == "xctrace" ]] && xcrun -f xctrace >/dev/null 2>&1; then
  OUT="$OUT_DIR/audio_studio_backend-$STAMP.trace"
  echo "Recording backend Time Profiler trace to $OUT"
  exec xcrun xctrace record \
    --template "Time Profiler" \
    --time-limit "${DURATION}s" \
    --output "$OUT" \
    --launch -- "$BIN" "$ROOT" "$PORT"
fi

if ! command -v sample >/dev/null 2>&1; then
  echo "No supported profiler found. Install Xcode Instruments or use macOS sample." >&2
  exit 1
fi

OUT="$OUT_DIR/audio_studio_backend-$STAMP.sample.txt"
"$BIN" "$ROOT" "$PORT" &
SERVER_PID=$!

cleanup() {
  kill "$SERVER_PID" >/dev/null 2>&1 || true
  wait "$SERVER_PID" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

for _ in {1..80}; do
  if curl -fsS "http://127.0.0.1:$PORT/api/projects" >/dev/null 2>&1; then
    break
  fi
  if ! kill -0 "$SERVER_PID" >/dev/null 2>&1; then
    wait "$SERVER_PID"
    exit 1
  fi
  sleep 0.25
done

if ! curl -fsS "http://127.0.0.1:$PORT/api/projects" >/dev/null 2>&1; then
  echo "Audio Studio backend did not become ready on port $PORT." >&2
  exit 1
fi

echo "Sampling backend pid $SERVER_PID for ${DURATION}s at ${INTERVAL_MS}ms intervals."
echo "Drive the UI in Chrome while this runs; results will be written to $OUT"
sample "$SERVER_PID" "$DURATION" "$INTERVAL_MS" -mayDie -fullPaths -file "$OUT"
echo "Backend sample written to $OUT"
