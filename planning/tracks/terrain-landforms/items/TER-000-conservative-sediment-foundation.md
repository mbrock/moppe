+++
id = "TER-000"
title = "Close the fluvial sediment ledger"
rfc = "RFC-0005"
track = "terrain-landforms"
status = "done"
depends_on = []
order = 0
areas = ["terrain", "hydrology", "storage"]
+++
# Close the fluvial sediment ledger

## Outcome

Stream-power detachment routes one solid sediment volume through the
production fractional drainage DAG, deposits or exports it, and persists
mobile cover and geological material history.

## Acceptance

- Synthetic chains, confluences, splits, sinks, and capacity breaks conserve
  volume.
- The production report closes detachment against deposition and ocean export.
- Trail earthwork remains separate from geological history.
- A pathological one-cell aggradation result is rejected and bounded.
- The finished-world cache invalidates the detachment-only schema and reloads
  the completed replacement.

## Evidence

`c3ae995`. The initial unconstrained Play capture produced vertical sediment
needles; the accepted implementation rate-limits local aggradation to 0.5 m
per 50,000-year step and carries excess downstream. All tests, an optimized
2048-square Play generation, a four-view sweep, and a version-3 cache miss,
save, and hit passed.
