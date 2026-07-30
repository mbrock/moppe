+++
id = "LIV-002"
title = "Give moisture its slope term"
rfc = "RFC-0003"
track = "living-world"
status = "ready"
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
