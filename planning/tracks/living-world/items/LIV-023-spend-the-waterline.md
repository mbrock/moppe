+++
id = "LIV-023"
title = "Spend the waterline contours the analysis already extracts"
rfc = "RFC-0003"
track = "living-world"
status = "ready"
depends_on = []
order = 150
areas = ["rendering", "water", "hydrology"]
+++
# Spend the waterline contours the analysis already extracts

## Outcome

Shorelines follow the terrain-scale water's edge instead of showing the grid.

## Scope

`Waterline` extracts the exact zero set of water-minus-ground per body by
marching squares. Its header names four intended consumers — conforming
geometry, shore ribbons, audio, and gameplay — and the world builds it, hands
it to a distance transform, and drops it on the floor. Meanwhile every water
capture shows a sawtooth staircase at the shore.

Give the contours a consumer. The cheapest is a depth-blended shore fragment:
lerp from the direct view ray toward full water shading by the fluid-to-ground
depth difference, which exists specifically to hide mesh artifacts at shores.
The contour polylines then carry the shore band, the swash phase that
`LIV-015`'s foam wants, and eventually the bank as a flow boundary.

## Acceptance

- Mouth and lake captures show a curved shoreline, no staircase.
- The contour value survives past `waterline_proximity` with a named consumer.

## Research

*Real-time Rendering of Enhanced Shallow Water Fluid Simulations* (Sheaf
`#CWC7H9`): depth-based shore blending as an artifact-hiding measure.
*Scalable real-time animation of rivers* (Sheaf `#XDESU9`): banks as flow
boundaries with a slip exponent.
