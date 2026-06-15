# Debug and Profile Guide

This project uses a standalone HTML frontend served by the C++ mock backend.
The VSCode setup therefore treats the backend process as the owner of the app
and launches or attaches Chrome for frontend debugging.

## Prerequisites

Install the recommended VSCode extensions when prompted:

- `ms-vscode.cmake-tools`
- `ms-vscode.cpptools`
- `vadimcn.vscode-lldb`

On macOS, backend profiling uses `/usr/bin/sample` by default. For graphical
Instruments traces, install full Xcode and run with `AUDIO_STUDIO_PROFILER=xctrace`.

## Build Configurations

The repository has isolated CMake presets:

```bash
cmake --preset debug
cmake --build --preset debug --parallel

cmake --preset profile
cmake --build --preset profile --parallel

cmake --preset release
cmake --build --preset release --parallel
```

`debug` builds to `build/debug` with `-O0`, debug symbols, and frame pointers.
`profile` builds to `build/profile` with optimization, debug symbols, and frame
pointers. `release` is separate from profiling and does not inherit profile
flags.

The existing product-style build remains unchanged:

```bash
./scripts/build_backend.sh
```

That script still builds `Release` into `build/`, so profiling/debug choices do
not affect the release path.

## One-Click Full Stack Debug

Open VSCode Run and Debug and choose:

```text
Full Stack: Debug
```

When prompted, keep port `8080` unless another process is using it.

This compound configuration:

1. Builds `build/debug/audio_studio_server`.
2. Starts the backend under LLDB.
3. Waits until `http://127.0.0.1:8080/api/projects` is reachable.
4. Opens Chrome with a dedicated workspace-local profile.
5. Lets you set breakpoints in both C++ and frontend JavaScript.

Backend breakpoints go in:

```text
GUI/backend/src/main.cpp
GUI/backend/src/http_server.cpp
GUI/backend/src/mock_runtime.cpp
GUI/backend/include/audio_studio.hpp
```

Frontend breakpoints go in:

```text
GUI/frontend/index.html
```

The files under `GUI/frontend/assets/js/` are tested helper/model modules, not the
current browser entrypoint.

Useful backend breakpoint locations:

- `HttpServer::handle` in `GUI/backend/src/http_server.cpp`
- `MockRuntimeEngine::buildPipeline` in `GUI/backend/src/mock_runtime.cpp`
- `MockRuntimeEngine::run` in `GUI/backend/src/mock_runtime.cpp`
- `FakeInspectorController::bufferLiveData` in `GUI/backend/src/mock_runtime.cpp`

Useful frontend breakpoint locations:

- `init`
- `buildPipeline`
- `runPipeline`
- `syncTelemetryFromBackend`
- `renderDashboard`

## Backend-Only Debug

Choose:

```text
Backend: Debug Server
```

Then open:

```text
http://127.0.0.1:8080
```

Use this when you only need C++ breakpoints or want to open the page in an
existing browser.

## Frontend-Only Debug

Start the backend first:

```bash
cmake --build --preset debug --parallel
./build/debug/audio_studio_server . 8080
```

Then choose:

```text
Frontend: Debug Chrome
```

Chrome opens with DevTools enabled. VSCode breakpoints work against files under
`GUI/frontend/`. Browser DevTools breakpoints also work normally.

If Chrome is already running with remote debugging on port `9222`, use:

```text
Frontend: Attach Chrome
```

## One-Click Full Stack Profile

Choose:

```text
Full Stack: Profile
```

When prompted:

- Port: `8080`
- Duration: for example `30`

This compound configuration:

1. Builds `build/profile/audio_studio_server`.
2. Starts backend sampling through `scripts/profile_backend.sh`.
3. Opens Chrome with DevTools for frontend profiling.
4. Writes backend samples to `profiles/backend/`.

While the backend sampler is running, drive the UI workflow you want to analyze:
load the page, build the pipeline, run it, select nodes/buffers, and interact
with dashboard panels. The backend profile stops after the requested duration.

## Backend Hotspot Analysis

Run from terminal or VSCode task:

```bash
AUDIO_STUDIO_PROFILE_SECONDS=30 ./scripts/profile_backend.sh 8080
```

The default macOS profiler is `sample`, producing:

```text
profiles/backend/audio_studio_backend-YYYYMMDD-HHMMSS.sample.txt
```

Open the sample file and look for the heaviest call-tree branches near the top.
Because the profile preset keeps symbols and frame pointers, C++ functions such
as `HttpServer::handle`, controller methods, and mock runtime methods should be
visible.

To use Instruments Time Profiler instead:

```bash
AUDIO_STUDIO_PROFILER=xctrace AUDIO_STUDIO_PROFILE_SECONDS=30 ./scripts/profile_backend.sh 8080
```

This writes an `.trace` package under `profiles/backend/` when `xctrace` is
available.

## Frontend Hotspot Analysis

Choose:

```text
Frontend: Profile Chrome
```

or use the full-stack profile compound. Chrome opens DevTools automatically.

Use these DevTools panels:

- `Performance`: record UI interactions and inspect long tasks, layout, paint,
  scripting, and event handlers.
- `Memory`: take heap snapshots if memory growth is suspected.
- `Network`: inspect API timing for `/api/*` calls.

Recommended frontend profile flow:

1. Open DevTools `Performance`.
2. Start recording.
3. Exercise one workflow, such as Build -> Run -> select buffer -> view probe.
4. Stop recording.
5. Inspect the heaviest scripting blocks and event handlers.

Common frontend hotspots to inspect:

- Node rendering and edge rendering.
- Dashboard refresh and telemetry synchronization.
- Inspector rendering after node or buffer selection.
- Real-time probe canvas drawing.

For repeatable command-line measurements, run:

```bash
npm run profile:frontend
```

The harness starts the local mock backend when needed, launches headless Chrome
through the Chrome DevTools Protocol, records idle UI metrics by default, and
writes JSON reports to:

```text
profiles/frontend/
```

Useful variants:

```bash
AUDIO_STUDIO_PROFILE_SCENARIO=running npm run profile:frontend
AUDIO_STUDIO_PROFILE_SCENARIO=both AUDIO_STUDIO_PROFILE_MS=30000 npm run profile:frontend
```

The report includes `Performance.getMetrics` deltas, Long Task totals,
requestAnimationFrame frame intervals, DOM node count, JS heap size, request
counts by API path, and active interval delays observed during page startup.

## Release Safety

The debug/profile setup is isolated from release behavior:

- Debug and profile builds use separate CMake preset build directories.
- Profile-only flags live in the `profile` preset.
- VSCode Chrome profiles live under `.vscode/chrome-*` and are ignored.
- Profile outputs live under `profiles/` and are ignored.
- `scripts/build_backend.sh` is unchanged and still builds Release.

Use `./scripts/build_backend.sh` or `cmake --build --preset release --parallel`
for release-style builds.

## Troubleshooting

If port `8080` is occupied, enter another port when VSCode prompts you.

If `Backend: Debug Server` fails with an LLDB adapter error, install the
recommended `vadimcn.vscode-lldb` extension and reload VSCode.

If Chrome fails to launch, check whether a stale dedicated profile exists under
`.vscode/chrome-debug-profile` or `.vscode/chrome-profile`; deleting those
ignored directories is safe.

If backend profiling says `xctrace` is unavailable, use the default `sample`
mode or install full Xcode. Command Line Tools alone may not include `xctrace`.
