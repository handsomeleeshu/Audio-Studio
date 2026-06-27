# Frontend Render Performance Notes

## Goal

Improve pipeline UI smoothness without changing visible behavior. The current
focus is main-thread rendering during runtime telemetry and ordinary user
operations such as canvas scrolling and selecting nodes.

Runtime telemetry now comes from GUI backend live APIs backed by as_server
System Info. Performance work must preserve that ownership: the frontend may
cache and render efficiently, but it must not replace PER-ALGORITHM COST, DSP
CORE LOADING, SYSTEM HEALTH, probe, or dump data with local fake values when
backend data is available.

## Findings

- The previous profile harness counted long tasks from page load, so startup
  work polluted idle and running measurements. The harness now resets page
  counters after warmup.
- `renderCostTable` had accumulated several compatibility post-processors. A
  normal render could build the table, then synchronously normalize it through
  multiple V54/V56/V59/V61/V62/V64 layers. Stopped state also scheduled extra
  `requestAnimationFrame`, `setTimeout`, and `MutationObserver` repairs.
- Node and edge selection used `renderAll(false)`. That preserved behavior, but
  it also refreshed cost, core, health, and meter panels for a selection-only
  change.

## Changes

- Added `__audioStudioProfileReset()` to the profile harness and a new
  `interaction` scenario that scrolls the canvas and selects nodes during the
  measured window.
- Added `applyCostTableFinalLayoutV99()` as the final cost table renderer. It
  calls the backend-owned V50 data renderer once and normalizes the final
  six-column layout in one pass.
- Removed the V62 stopped-state async repair churn from ordinary cost table
  renders.
- Added `renderSelectionChange()` for selection-only updates. It updates node
  selected classes, edge visuals, and Inspector state without refreshing
  unrelated dashboard panels.
- Runtime refresh paths treat Build as all-pipeline and RUN as selected-pipeline
  state, so dashboard refresh should follow `PIPE_RUNNING` for the active group
  without changing loaded state for the rest of the layout.

## Evidence

6 second local profile runs on June 12, 2026:

| Scenario | Before | After |
| --- | ---: | ---: |
| Interaction p95 frame | 216.7 ms | 16.8 ms |
| Interaction frames over 50 ms | 21 | 0 |
| Interaction long tasks | 28 | 0 |
| Interaction TaskDuration | 4804.2 ms | 1406.0 ms |
| Interaction LayoutDuration | 771.4 ms | 226.1 ms |

The interaction benchmark now stays in the 60 fps frame budget while still
performing canvas scroll and node selection work.

## Regression Coverage

- `tests/frontend/runtime-loop-policy.test.mjs`
- `tests/frontend/selection-render-performance.test.mjs`
- `tests/frontend/performance-profile.test.mjs`
- `tests/frontend/gui-runtime-contract.test.mjs`
