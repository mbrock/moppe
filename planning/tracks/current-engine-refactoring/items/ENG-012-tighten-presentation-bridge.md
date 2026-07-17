+++
id = "ENG-012"
title = "Keep quantity-to-texture conversion inside presentation bridges"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "done"
depends_on = ["ENG-011"]
order = 50
areas = ["game", "render", "map"]
+++

# Keep quantity-to-texture conversion inside presentation bridges

## Outcome

`SurfacePresentation` and `WaterPresentation` are the sole conversion points
from intrinsic quantities to renderer texture lanes and normalized scalars.

## Scope

Audit existing uploads and migrate them to the bridge. Keep renderer contracts
concrete; this is not a general serialization layer.

## Acceptance

- Gameplay and terrain rules retain typed quantities.
- GPU-facing floats are produced only at a named presentation boundary.
- Existing terrain and water captures remain equivalent.

## Evidence

`SurfacePresentation` owns every terrain-material texture upload. Its narrow
path route converts a rebuilt `TrailNetwork` at the same boundary, while a
pristine Terrain Lab reuses the already materialized path payload; Terrain Lab
no longer calls `Renderer::set_terrain_paths` itself. `WaterPresentation` now
receives a metre-valued datum and typed world extent, constructs the numeric
`OceanSetup` internally, and remains the only water level/flow packing and
upload boundary. `tests/recording_renderer.hh` lets the focused surface and
water tests assert the actual renderer payloads, including preview-path
expansion and ocean setup.

`./tools/format --check`, `cmake --build build -j 2`, `./build/moppe-tests`
(192 tests), and `ctest --test-dir build --output-on-failure` pass. Deterministic
`tools/capture-water /tmp/moppe-eng012-water.png river` and
`tools/capture-terrain-lab /tmp/moppe-eng012-terrain-lab.png` both completed
and produced valid PNG captures.
