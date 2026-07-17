+++
id = "ENG-023"
title = "Hand completed worlds across one asynchronous boundary"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "done"
depends_on = ["ENG-022"]
order = 90
areas = ["world", "platform", "game"]
+++

# Hand completed worlds across one asynchronous boundary

## Outcome

Background construction produces a complete `GeneratedWorld`; the main thread
activates it and creates presentation resources without observing half-built
state.

## Scope

Keep progress reporting and cancellation policy concrete. Do not make general
async infrastructure.

## Acceptance

- The loading screen observes progress but does not own terrain state.
- Activation has one main-thread handoff point.
- Generation failures retain the current clear failure behavior.

## Evidence

`MoppeGame` now creates one `GenerationJob` at a time. Its worker builds a
fresh `GeneratedWorld` with a worker-local field evaluator, publishes that
owner only after terrain, hydrology, and materialization complete, and copies
only local height snapshots to the loading preview. The platform's main-thread
completion callback is the sole `activate_completed_world()` ownership commit:
Terrain Lab leaves, vehicle/car/glider borrowers are rebuilt, then the
completed owner becomes live. Stars, water inspection, GPU uploads, shadows,
and other presentation stay on the main thread after that boundary. Failures
still log and exit; running builds are deliberately never cancelled. The
platform retains the shared job context through its callback, while a job
lifetime mutex serializes worker/callback game access with `MoppeGame`
teardown. Closing during generation waits for the worker to finish; its queued
callback sees a cleared game pointer and does nothing.

`generated_world_handoffs_move_the_owner_not_the_world` verifies the
non-movable owner transfer. `./tools/format --check`, `git diff --check`,
`cmake --build build -j 2`, `./build/moppe-tests` (193 tests),
`ctest --test-dir build --output-on-failure`, and `./tools/plan check` passed.
The deterministic normal water, Terrain Lab, and in-process regeneration
captures wrote `/tmp/moppe-eng023-lifetime-water.png`,
`/tmp/moppe-eng023-final-lab.png`, and
`/tmp/moppe-eng023-final-regen.png`. An uncached loading capture wrote
`/tmp/moppe-eng023-shutdown-active.uJh8UV/loading.png` while geological
evolution was running, then exited cleanly without a lingering process.
