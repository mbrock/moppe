+++
id = "ENG-012"
title = "Keep quantity-to-texture conversion inside presentation bridges"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "ready"
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
