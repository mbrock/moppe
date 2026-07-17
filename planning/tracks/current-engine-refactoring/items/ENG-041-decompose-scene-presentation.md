+++
id = "ENG-041"
title = "Decompose scene presentation around FrameView"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "done"
depends_on = ["ENG-040", "ENG-012"]
order = 150
areas = ["scene", "game", "render"]
+++
# Decompose scene presentation around FrameView

## Outcome

World, actors, effects, overlays, and HUD become focused presentation routines
that consume the same immutable frame reading.

## Scope

Move existing drawing paragraphs out of `MoppeGame::render`; retain the
game-shaped `Renderer` API until a concrete new need proves otherwise.

## Acceptance

- `MoppeGame::render` handles application-state selection and delegates.
- The renderer is exercised by equivalent captures before and after extraction.

## Evidence

`MoppeGame::render` now selects loading, game-over, and ready-world state,
composes one `FrameView`, requests captures, then delegates explicit world,
actor, water, effect, and overlay layers. `FrameHud` and `FrameOverlay` freeze
the gameplay HUD, trail-map, and cinematic-prompt readings; only retained
stars and dust presentation resources remain read-only session borrows.

Validation on 2026-07-17:

- `tools/format --check`, `git diff --check`, `cmake --build build -j 2`,
  `./build/moppe-tests` (203 passing), `ctest --test-dir build
  --output-on-failure`, and `tools/plan check` passed.
- Before/after captures were visually compared for `tools/capture-game`,
  `tools/capture-terrain-lab`, `tools/capture-cinematic ... 4`,
  `tools/capture-water ... river`, and `tools/capture-trees ... 9`.
- `frame_view_snapshots_hud_and_overlay_readings` characterizes the frozen
  HUD/trail and cinematic-overlay readings.
