+++
id = "LIV-001"
title = "Grade terrain material against the world's own relief"
rfc = "RFC-0003"
track = "living-world"
status = "done"
depends_on = []
order = 10
areas = ["rendering", "terrain", "units"]
+++
# Grade terrain material against the world's own relief

## Outcome

Altitude material bands read as fractions of the land relief above sea level
rather than as bare metre thresholds, on both backends.

## Scope

The band expressions and the one uniform that carries the relief. No change to
the band values themselves, to the textures, or to any generation pass.

## Acceptance

- A default seed shows grass, dirt, rock, and snow in altitude order.
- The swash band remains a real width in metres.
- WebGPU grades identically.

## Evidence

`91f04cf`. Seed 123 land runs 50 m to 490 m; the snow band began at 0.55 m, so
every land cell was fully snow-covered and the grass and dirt layers were
unreachable. `Terrain::setup` now measures the surface's own height range and
sends the relief through `TerrainParams::land_relief`. The `capture-trees` view
of seed 123 goes from a uniform snowfield to green hills under a snowcapped
peak with a visible river, on identical geometry.
