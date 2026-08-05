+++
id = "TER-010"
title = "Route hillslope sediment conservatively across faces"
rfc = "RFC-0005"
track = "terrain-landforms"
status = "backlog"
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
