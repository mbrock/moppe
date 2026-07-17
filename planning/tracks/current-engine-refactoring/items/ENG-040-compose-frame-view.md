+++
id = "ENG-040"
title = "Compose immutable FrameView values for presentation"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "done"
depends_on = ["ENG-031"]
order = 140
areas = ["scene", "render", "simulation"]
+++
# Compose immutable FrameView values for presentation

## Outcome

Camera, lighting, weather, actor poses, and mode-specific visible state are
composed before renderer encoding into a plain frame reading.

## Scope

Do not create a generic scene graph. This is the concrete presentation value
needed by Moppe's existing renderer.

## Acceptance

- Rendering reads a world, session, and frame view without mutating simulation.
- Camera shake, cinematics, and Terrain Lab views remain visibly equivalent.

## Evidence

`game::FrameView` is a plain, copyable presentation reading composed from a
selected camera, immutable world inputs, a const `GameSession`, and graphics
settings. It snapshots bike/car/glider/walker poses, actor scale, lighting,
weather, mode-specific visibility, benchmark coordinates, and both the
unshaken culling camera and the shaken billboard basis. The existing actor
renderers now take those poses rather than live simulation objects.

`MoppeGame::tick` advances the lens-flare accumulator after each active camera
update, including cinematic, Terrain Lab, tree-demo, and ordinary-play paths.
`MoppeGame::render` only composes the view, encodes `FrameParams`, and draws
from it; it no longer mutates `GameLogicState`.

Focused tests verify a read-only deterministic composition, frozen actor
snapshots, shake basis separation, cinematic and Terrain Lab rules, visual
scale and sun-height capture, and clear/water/ridge/cloud flare targets.
`./tools/format --check`, `git diff --check`, `cmake --build build -j 2`,
`./build/moppe-tests` (202 tests), `ctest --test-dir build --output-on-failure`,
and `./tools/plan check` passed. Manual before/after captures of normal play,
Terrain Lab, and a four-second cinematic were visually equivalent.
A compact live benchmark wrote 64 samples across 32 epochs to
`/tmp/moppe-eng040-benchmark.csv`.
