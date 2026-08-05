+++
id = "TER-030"
title = "Set an honest trunk-river and world scale"
rfc = "RFC-0005"
track = "terrain-landforms"
status = "backlog"
depends_on = ["TER-011", "TER-021"]
order = 80
areas = ["terrain", "rivers", "world-recipe", "rendering"]
+++
# Set an honest trunk-river and world scale

## Outcome

The physical world extent, runoff interpretation, channel width, water depth,
and valley width agree on what the largest watercourse in a 5 km world is, or
the recipe changes scale explicitly.

## Acceptance

- The maximum catchment and intended river class are stated in physical units.
- Landform width and rendered water width describe the same river.
- `stream`, `river`, `confluence`, and `mouth` captures show a monotone and
  believable hierarchy at motorcycle scale.
