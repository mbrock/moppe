+++
id = "TER-021"
title = "Deposit sediment across valley-width footprints"
rfc = "RFC-0005"
track = "terrain-landforms"
status = "ready"
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
