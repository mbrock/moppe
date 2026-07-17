+++
id = "ENG-033"
title = "Run graphics benchmarks through session replay"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "ready"
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
