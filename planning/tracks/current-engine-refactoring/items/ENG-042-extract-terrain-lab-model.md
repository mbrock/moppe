+++
id = "ENG-042"
title = "Separate TerrainLabModel from lab UI and overlays"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "ready"
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
