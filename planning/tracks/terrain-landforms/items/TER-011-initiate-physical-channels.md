+++
id = "TER-011"
title = "Initiate channels above a physical catchment scale"
rfc = "RFC-0005"
track = "terrain-landforms"
status = "backlog"
depends_on = ["TER-021"]
order = 60
areas = ["terrain", "hydrology", "analysis"]
+++
# Initiate channels above a physical catchment scale

## Outcome

Concentrated fluvial incision begins at an inspectable physical drainage area,
while smaller catchments retain detail from the accepted hillslope process.

## Acceptance

- Channel-head scale is resolution-aware in physical units.
- Drainage density and spectral evidence improve without recreating the dull
  result recorded in `docs/hillslopes-and-channels.md`.
- Fixed renderer views and riding show branching hierarchy, varied valley
  spacing, and useful small-scale ground.

## Renewed dependency finding

The initial matrix cannot honestly select this scale yet. Thresholds that are
large enough to mean the same thing at 1024 and 2048 resolution suppress the
incision that currently drains small basins, producing perched water and sheer
channel walls. A 7 m2 numerical compromise works only because it is about one
2048-grid cell and disappears at 1024. TER-020 and TER-021 now precede this
item so cover feedback and valley-floor deposition can receive the sediment
before the physical threshold is tried again. See `findings.md`.

The post-TER-021 matrix confirms that those two processes are insufficient on
their own. At the first resolved 25 m2 threshold, median slope reaches 47.8
degrees and inland water area reaches 1.59 km2. At 100 m2, median slope is
70.0 degrees and connected gentle ground nearly disappears. The 400 m2 world
cannot form a complete trail circuit. TER-022 now precedes another attempt:
standing-water accommodation and outlet evolution must be explicit before
small-catchment incision can stop serving as the world's accidental basin
drainage process.
