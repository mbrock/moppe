+++
id = "LIV-020"
title = "Render river junctions as a field, not a mesh"
rfc = "RFC-0003"
track = "living-world"
status = "backlog"
depends_on = ["LIV-015"]
order = 120
areas = ["rendering", "water"]
+++
# Render river junctions as a field, not a mesh

## Outcome

A confluence reads as one watercourse, with no dark overlap wedge, and rivers
cost less than they do now.

## Scope

The confluence overlap is not a bug in the junction geometry; it is a property
of having junction geometry at all. Tessellating each reach separately means
the strips overlap, and a blended fan was already tried and rejected for
darkening the overlap it was meant to hide.

The alternative is to stop meshing the water. Draw each reach as a small
bounding quad and evaluate, per fragment, the distance to the reach's curve —
closed form for a quadratic, no iteration — discarding beyond the
catchment-derived half-width. The arc length along the curve is the flow
coordinate. At a junction, one larger quad covers the group and each fragment
projects onto every member curve, interpolating by distance. There is no
junction to build, so there is nothing to overlap.

Everything this needs, Moppe already has: `RiverAlignment` is the curve,
catchment-derived width is the threshold, the globally continuous downstream
distance is the flow coordinate, and body identity says where the field yields
to a sheet. The seam-crossing triangle class disappears with the triangles.

Junction velocity should come from a discharge-weighted mean of the inlet
reaches. Blending opposed tangents is a documented artifact source.

## Acceptance

- Feature-targeted confluence captures show one surface, no wedge.
- River cost in the graphics benchmark does not increase.
- Periodic seams stay clean without the current nearest-image special case.

## Research

*Real-time Rendering of River Networks* (Sheaf `#MVUJ8Z`): per-pixel Bézier
distance fields, junction grouping, screen-space-proportional cost.
*Procedural Riverscapes* (Sheaf `#AK7NGE`): blend operators, and the warning
about blending disparate velocities.
