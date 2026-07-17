+++
id = "ENG-043"
title = "Split Metal resource ownership from concrete frame passes"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "done"
depends_on = ["ENG-041"]
order = 170
areas = ["render", "metal"]
+++
# Split Metal resource ownership from concrete frame passes

## Outcome

Retained terrain/water resources and per-frame terrain, water, scene, post,
and HUD passes have explicit ownership and inputs.

## Scope

Refactor the backend after scene inputs have stabilized. Keep the public
renderer interface game-shaped and avoid a generic render graph framework.

## Acceptance

- `MetalRenderer` no longer contains all pass sequencing in one method family.
- GPU benchmark and capture behavior remain available.

## Evidence

`MetalRenderer` now routes retained terrain, water, and frame-target ownership
through `MetalTerrainResources`, `MetalWaterResources`, and
`MetalFrameTargets`; a per-drawable `MetalFrameEncoding` carries command
buffer, stream, timing, and capture state. Concrete Terrain, Water, Scene,
Post, and HUD operations receive those explicit inputs while preserving the
single shared scene encoder and the renderer-owned timing/capture lifecycle.

Validation completed on macOS Metal:

- _Build and portable tests:_ a normal Ninja build, `./build/moppe-tests`
  (203 passes), and `ctest --test-dir build --output-on-failure` pass.  A
  separate non-unity configuration also builds the `moppe` and
  `moppe-testbed` targets, including `metal_renderer.mm` as its own
  Objective-C++ translation unit.
- _Metal smoke and presentation:_ `MOPPE_FRAMES=30 ./build/moppe-testbed`,
  deterministic game and Terrain Lab screenshots, river and mouth water
  captures, and a four-second cinematic all complete and render correctly.
- _Lifecycle coverage:_ the compact graphics benchmark writes all 64 samples
  and its analysis, while a ready-world `MOPPE_METAL_CAPTURE` runs for 20
  gameplay frames and writes a trace plus screenshot.
