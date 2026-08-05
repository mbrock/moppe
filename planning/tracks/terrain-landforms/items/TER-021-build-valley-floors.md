+++
id = "TER-021"
title = "Deposit sediment across valley-width footprints"
rfc = "RFC-0005"
track = "terrain-landforms"
status = "done"
depends_on = ["TER-020"]
order = 50
areas = ["terrain", "rivers", "sediment"]
+++
# Deposit sediment across valley-width footprints

## Outcome

Channel deposition occupies a width derived from trunk scale and local valley
geometry instead of painting a one-cell centerline, producing connected
alluvial floors and downstream widening.

## Acceptance

- Lateral postings conserve the routed solid volume.
- Width varies continuously with drainage scale and remains physical across
  resolutions.
- Synthetic slope breaks build a floor rather than a ridge or raster terrace.
- Gazetteer views contain rideable connected low-gradient valley land.

## Evidence

The lateral stage preserves the routed volume exactly while changing only its
destination cells. Its full width is a continuous meter-valued function of
the square root of contributing area, bounded from 6 to 160 m, and local wall
relief clips the footprint to receiving valley ground. Synthetic tests prove
exact lateral conservation and raise a three-cell slope-break bottom as one
level floor rather than a centerline ridge.

In the optimized seed-123 Play world, the largest connected land at or below
ten degrees grew from 0.138 to 0.423 km2 while relief changed from 197.7 to
194.6 m. The complete sixteen-view gazetteer finished in 113 seconds and the
saved cache loaded on the immediate verification launch. Full evidence is in
`../findings.md`.
