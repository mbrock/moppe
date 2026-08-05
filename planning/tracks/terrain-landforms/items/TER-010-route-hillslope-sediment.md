+++
id = "TER-010"
title = "Route hillslope sediment conservatively across faces"
rfc = "RFC-0005"
track = "terrain-landforms"
status = "done"
depends_on = ["TER-002"]
order = 30
areas = ["terrain", "sediment", "units"]
+++
# Route hillslope sediment conservatively across faces

## Outcome

The legacy Laplacian elevation edit becomes an explicit, slope-limited solid
volume flux across neighbouring cell faces, producing convex hills and
colluvial footslopes while closing its own ledger.

## Acceptance

- Every face transfer posts equal and opposite volume.
- Fixed ocean boundaries have an explicit export or no-flux rule.
- The scheme is stable across the accepted geological time steps.
- Mobile cover moves before bedrock is exposed.
- Synthetic convex and concave profiles evolve in the expected directions.

## Evidence

- `route_hillslope_sediment` enumerates each periodic east and south face,
  posts one downhill `-V/+V` pair, and reports its signed residual.
- Faces touching the fixed mask are explicitly no-flux. Stable internal
  sweeps retain the former physical diffusivity across long time steps.
- Existing mobile sediment supplies outgoing transfers before the uncovered
  remainder is counted as bedrock detachment. Both histories enter the shared
  stream-power material ledger.
- Synthetic ledger, fixed-boundary, cover-first, convex, concave, and
  long-interval tests pass with the complete suite.
- The optimized 2048-square seed-123 Play world completed all sixteen frozen
  views in 96 seconds and hit cache version 6 on its next launch. Shape metrics
  remain within 0.3% of the selected baseline while internal hillslope solid
  movement is now explicit. See [`findings.md`](../findings.md).
