+++
id = "TER-011"
title = "Initiate channels above a physical catchment scale"
rfc = "RFC-0005"
track = "terrain-landforms"
status = "done"
depends_on = ["TER-021", "TER-022", "TER-025"]
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

TER-022 supplies basin accommodation and materially expands connected
low-gradient land while retaining mountain relief. Its renewed matrix proves
that accommodation alone is insufficient. Thresholds from 7 to 10 m2 make
excellent lowlands but coincide with one 2048-grid cell and lose the
confluence target. At the first clearly resolved 25 m2 threshold, inland water
reaches 3.10 km2 and the 90th-percentile slope reaches 49.7 degrees; at 100 m2
they reach 3.73 km2 and 62.4 degrees. The ordinary channel-head law can
suppress incision at the census-designated dry spill cell even though lake
overflow has already concentrated there. TER-023 must give that spillway a
conservative erosion path before this item can select a physical scale. The
optimized A/B rejected that hypothesis: water area changed by only 1%, while
slope and missing confluence were unchanged. TER-023 was dropped. TER-024 now
tested the broader zero-runoff discontinuity. A 0--20% diffuse share slightly
lowered upper-tail slope but did not grow rolling land, retained the fluting,
and eventually lost the confluence target. It was also dropped. TER-025 now
tests a genuinely distinct hillslope law: slope-selective conservative wasting
without any below-channel bedrock incision.

## Decision

Carry a 100 m2 channel-initiation area into TER-030, paired with TER-025's 0.6
critical gradient and multiplier 4. Unlike the earlier 7--10 m2 coincidence,
100 m2 spans about seventeen cells at 2048 and four cells at 1024. Both
resolutions produce rolling land, mountain groups, completed trails, lakes,
river reaches, and mouths without restoring all-cell incision.

At 2048 the selected world has 257 m relief, 19.7-degree median slope, 51.2%
of land below twenty degrees, a 5.71 km2 connected rolling region, 22.3 km of
visible river, and all seventeen gazetteer targets. At 1024 it has 263 m
relief, 19.3-degree median slope, 52.6% below twenty degrees, a 9.63 km2
connected rolling region, and 19.6 km of river. The coarser analysis cannot
select a confluence target even though river and mouth targets remain; that
resolution-sensitive river hierarchy is handed to TER-030 rather than hidden
by lowering the physical channel head.

The Play defaults remain unchanged until TER-040 accepts a multi-seed world.
Decision artifacts are under `/tmp/moppe-hillslope-refined.0DLaGE` and
`/tmp/moppe-hillslope-1024.euWbKA`.
