+++
id = "LIV-002"
title = "Give moisture its slope term"
rfc = "RFC-0003"
track = "living-world"
status = "done"
depends_on = ["LIV-001"]
order = 20
areas = ["hydrology", "ecology", "quantities"]
+++
# Give moisture its slope term

## Outcome

A typed soil-wetness reading that combines upslope contributing area with
local slope, alongside the existing dampness used for material rendering.

## Scope

`terrain::MoistureParameters` blends proximity-to-water with log-compressed
drainage accumulation and no slope at all, so a flat valley floor and a steep
gully with the same upstream area read equally wet. Add the topographic
wetness index — `ln(a / tan β)` — as its own `QUANTITY_SPEC`, computed from the
accumulation already in `FractionalDrainage` and the slope already in the
surface normals. Keep `surface_moisture` as the material dampness it is; the
new quantity is a plant-viability reading with a different meaning.

FastFlow notes that ecosystem work prefers multiple-flow-direction
accumulation where channel work prefers single-receiver, because a vegetation
gradient wants diffuse water and a river does not. If the D-infinity
fractional area is too channelized to read as soil moisture, that is the
reason, and a second diffuse accumulation is the answer rather than blurring
the first.

## Acceptance

- The reading is a named quantity with units, stored in `SurfaceReadings`.
- A valley floor reads wetter than a gully of equal catchment.
- An overlay capture shows riparian corridors, not just a halo around water.

## Research

FastFlow (Sheaf `#NV2YRW`): soil moisture as a viability criterion, TWI as the
standard per-cell proxy, and the SFD/MFD distinction.

## Evidence

`soil_wetness` is a `proportion` computed in `analyze_moisture` alongside the
material dampness, from the catchment already in `DrainageGraph` and the slope
it already carries: the index is the difference of two logarithms, octaves of
upstream cells less octaves of fall. `analyze_tree_habitat` reads it instead of
`surface_moisture`, so a tree responds to what the soil holds rather than to
how near the view is to open water.

The habitat bands were set from the index's measured distribution over a
generated world rather than guessed: the driest tenth sits at zero, the median
hillside near a fifth, and the wettest tenth is standing water and the flats
around it.

The first attempt collapsed the forest from 35,651 canopy representatives to
1,565, which turned out to be a second constant of the same family as LIV-001:
a tree line fixed at 145 m above the datum on a world with 500 m of relief left
only 11.4% of the ground below it. The tree line is now a share of the world's
own relief, placed just under where the terrain shader begins to hold snow.
