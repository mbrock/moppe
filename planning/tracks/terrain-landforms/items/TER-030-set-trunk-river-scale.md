+++
id = "TER-030"
title = "Set an honest trunk-river and world scale"
rfc = "RFC-0005"
track = "terrain-landforms"
status = "done"
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

## Evidence

The discrepancy was extraction. The old visible-river threshold was the area
whose rendered width reached two cells: about 0.174 km2 at 2048 but 0.694 km2
at 1024. Resolution therefore changed the physical river class and deleted
tributary reaches before the capture selector ever saw them.

Visible extraction now begins at a physical five-metre water width, or
0.174 km2 under the existing hydraulic law, independent of the generation
lattice. At that lower boundary the depositional valley-width law assigns a
22.7 m floor. The seed-123 trunk drains 13.1--13.9 km2 across the two tested
resolutions, renders at the 24 m width and 2.5 m depth caps, and receives an
approximately 150 m depositional floor. Both laws consume the same typed
contributing area and remain continuous between those endpoints.

Re-analysis of the already generated calibrated terrains produced all 17
gazetteer views at both 1024 and 2048, including `stream`, `river`,
`confluence`, and `mouth`. The fixed views show narrow headwater water,
widening reaches, joined tributaries, and the broadest water at the mouth.
Cache schema 12 prevents resolution-dependent version-11 river topology from
being mistaken for the new result.

Decision artifacts are under `/tmp/moppe-river-1024.koPI8c` and
`/tmp/moppe-river-2048.B3UBjA`.
