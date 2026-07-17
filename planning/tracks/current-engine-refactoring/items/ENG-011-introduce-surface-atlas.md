+++
id = "ENG-011"
title = "Make SurfaceAtlas own named materialization groups"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "ready"
depends_on = ["ENG-010"]
order = 40
areas = ["map", "spatial"]
+++

# Make SurfaceAtlas own named materialization groups

## Outcome

Surface geometry, hydrology, geology, ecology, and use become named groups of
typed sections over one surface domain, with their own explicit barriers.

## Scope

This reorganizes existing materialized readings. It does not replace the
authoritative heightmap or invent a new terrain-generation kernel.

## Acceptance

- A reader can enumerate which sections exist and when each becomes valid.
- Zero-valued readings remain distinguishable from unavailable readings.
- Existing consumers use named atlas views rather than a growing Boolean ledger.
