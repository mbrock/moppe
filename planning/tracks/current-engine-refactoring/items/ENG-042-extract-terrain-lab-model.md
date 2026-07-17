+++
id = "ENG-042"
title = "Separate TerrainLabModel from lab UI and overlays"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "done"
depends_on = ["ENG-020"]
order = 160
areas = ["terrain-lab", "terrain", "scene"]
+++
# Separate TerrainLabModel from lab UI and overlays

## Outcome

Editable terrain-program state, checkpoints, and evaluation progress live in a
renderer-independent model; UI and overlay presentation become consumers.

## Scope

Use transform-local editing semantics from ENG-020. Do not turn the Terrain
Lab into a separate application.

## Acceptance

- Property normalization switchboards disappear in favor of typed transform
  editors.
- The model can be exercised without a Metal renderer.

## Evidence

`game::TerrainLabModel` owns the editable terrain program, map snapshot,
CPU-or-accelerated evaluator, checkpoints, reports, trail result, and
evaluation progress without depending on platform or renderer types.
`TerrainLab` now presents that model and retains only UI, camera, overlay, and
renderer-facing state. `terrain::TerrainProgramEditor` delegates normalized and
natural edits to concrete source and transform editors, including the orogeny
source sea-level invariant, so the Lab no longer contains property-type or
normalization switchboards. Focused model and editor tests run without Metal.
`cmake --build build -j 2`, `./build/moppe-tests` (190 tests), and
`ctest --test-dir build --output-on-failure` passed.
