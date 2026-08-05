+++
id = "TER-030"
title = "Set an honest trunk-river and world scale"
rfc = "RFC-0005"
track = "terrain-landforms"
status = "ready"
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

## Starting evidence

TER-011 selects a 100 m2 channel head on the five-kilometre world. At 2048 it
produces 22.3 km of visible river and all water targets; at 1024 it produces
19.6 km but loses the confluence selection while keeping river and mouth views.
This item must determine whether the discrepancy is extraction, width/depth
presentation, or an honest limit of the current physical extent.
