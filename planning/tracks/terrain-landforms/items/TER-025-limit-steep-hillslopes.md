+++
id = "TER-025"
title = "Limit steep hillslopes conservatively"
rfc = "RFC-0005"
track = "terrain-landforms"
status = "done"
depends_on = ["TER-022"]
order = 78
areas = ["terrain", "sediment", "hillslopes"]
+++
# Limit steep hillslopes conservatively

## Outcome

The hillslope flux accelerates as a face approaches a physical critical
gradient, rounding steep interfluves and building colluvial footslopes without
giving small catchments stream-power bedrock incision.

## Acceptance

- The linear law remains the exact multiplier-one case.
- The nonlinear factor is dimensionless, bounded, and included in explicit
  stability substeps.
- Every accelerated face transfer remains cover-first and closes the existing
  solid-volume ledger.
- Synthetic subcritical slopes retain the linear result while steep convex
  slopes move more material without inversion or raster terraces.
- An optimized resolved-channel world lowers the steep-slope tail and expands
  connected rolling land without losing river, confluence, or trail targets.

## Evidence

The multiplier-one path reproduces linear creep exactly. Subcritical synthetic
faces remain byte-identical even when a higher cap is available; critical
faces move more cover-first solid, choose a correspondingly stable sweep
count, and close the volume ledger.

At seed 123 and a 25 m2 channel head, a 0.8 critical gradient with multiplier
4 lowered P90 slope from 49.7 to 34.2 degrees and spectral excess from +0.644
to +0.416 dex while retaining all seventeen views. Generation rose only from
120 to 134 seconds. A refined 0.6 gradient was then carried into the physical
channel decision: it expands connected rolling-angle ground while leaving
ordinary subcritical creep unchanged.

Decision artifacts are under `/tmp/moppe-critical-hillslope.x8Cjln` and
`/tmp/moppe-hillslope-refined.0DLaGE`.
