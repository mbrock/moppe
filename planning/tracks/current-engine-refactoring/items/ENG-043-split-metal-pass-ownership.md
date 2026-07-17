+++
id = "ENG-043"
title = "Split Metal resource ownership from concrete frame passes"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "backlog"
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
