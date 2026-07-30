+++
id = "LIV-022"
title = "Grow the falling sheet from the lip line"
rfc = "RFC-0003"
track = "living-world"
status = "backlog"
depends_on = ["LIV-021"]
order = 140
areas = ["rendering", "water"]
+++
# Grow the falling sheet from the lip line

## Outcome

A waterfall has a curtain and a foot, and neither intersects the terrain.

## Scope

A vertical quad over the heightfield was tried and rejected: it cut into the
ground and broke the river. The reason is categorical rather than technical. A
lip is a landform that persists; a falling sheet is a process that happens.
Making the process into static geometry is what failed.

So build it the way the process works. `FallLip` becomes a per-world finite
domain beside `Waterline` and `WaterBodyDomain` — a polyline extracted the same
way the shoreline is. Each frame, spawn particles along the visible lips,
inherit the line's connectivity so successive layers form quads into a closed
sheet, advect under gravity, and kill on contact with the receiving surface.
Texture coordinates come free: lifetime along one axis, position in the line
along the other. Spray spawns where sheet particles die.

Nothing static exists to intersect the ground, and the whole thing is inert for
falls that are not on screen.

## Acceptance

- The waterfall capture shows a lip, a curtain, and a foot.
- No terrain intersection at any camera angle on the benchmark candidates.
- Off-screen falls cost nothing.

## Research

*Real-time Breaking Waves for Shallow Water Simulations* (Sheaf `#8SERGP`):
particle sheets spawned along a detected line, connectivity-preserving
refinement, thickness by displaced duplication, lifetime-based texturing,
splash particles at contact. *Procedural Riverscapes* (Sheaf `#AK7NGE`) is
explicit that a height field cannot produce this and stops at the lip and the
plunge pool.
