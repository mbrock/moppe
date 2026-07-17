+++
id = "ENG-033"
title = "Run graphics benchmarks through session replay"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "done"
depends_on = ["ENG-032"]
order = 130
areas = ["benchmark", "simulation", "render"]
+++
# Run graphics benchmarks through session replay

## Outcome

The graphics benchmark restores a session checkpoint, resets renderer history,
and replays ordinary input frames without benchmark branches throughout game
simulation.

## Scope

Preserve the existing feature cube, Gray-code traversal, and command-buffer
timing evidence.

## Acceptance

- Benchmark results retain their current columns and interpretation.
- Benchmark-only control flow is removed from ordinary simulation where the
  public replay seam replaces it.

## Evidence

`GraphicsBenchmarkReplay` materializes separate prelude and replay input tapes
and returns the pre-step replay coordinate for each ordinary fixed-step call.
`MoppeGame` captures and restores `GameSession::state()` at replay boundaries,
resets renderer history, applies the Gray-code graphics mask, and passes the
exact frame coordinate to rendering. `advance_game_session` remains free of
benchmark state and branches.

`graphics_benchmark_replay_reuses_the_public_session_tape` drives a two-frame
prelude and a one-settle/two-measured-frame replay through the public advance
operation, verifies tape reuse and Gray-code metadata, and produces 64
measured outputs across the 32 configurations. The CSV feature list now uses
the same included riding-feature order as its resolved mask: it excludes the
debug terrain-topology overlay and includes snow-support-filter and
channel-flux-detail.

`./tools/format --check`, `git diff --check`, `cmake --build build -j 2`,
`./build/moppe-tests` (196 tests), `ctest --test-dir build --output-on-failure`,
and `./tools/plan check` passed. A live short benchmark wrote 64 samples with
32 epochs to `/tmp/moppe-eng033.RCWIL2/gpu.csv`; its DuckDB analysis completed
through `tools/graphics-benchmark-analyze`.
