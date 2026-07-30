+++
id = "LIV-012"
title = "Negotiate the crown-to-cover handoff"
rfc = "RFC-0003"
track = "living-world"
status = "backlog"
depends_on = ["LIV-011"]
order = 80
areas = ["rendering", "vegetation", "lod"]
+++
# Negotiate the crown-to-cover handoff

## Outcome

Trees stop popping at the boundary between crown geometry and the filtered
cover material.

## Scope

`forest_cover` darkens the ground beyond the crown draw distance through a
blind crossfade over 280 m to 1450 m. The two representations never negotiate:
the far field does not know what the near field drew, so the transition is a
dissolve between two different-looking things.

Two moves, both from the literature and both cheap:

- Fade the *animation* and sink crowns vertically into the ground with
  distance, so a tree leaves by settling rather than by vanishing.
- Make the coverage the near field actually draws the value the far field
  fades toward, so the two agree at the seam.

This is `ideas/near-and-far.md`'s two-register forest with the registers
finally introduced to each other.

## Acceptance

- A slow dolly out across a stand shows no pop and no brightness step.
- Cover density at the seam matches drawn crown coverage.

## Research

Bruneton and Neyret (Sheaf `#BDBBL6`): switch when apparent tree size is about
one pixel; force the detailed representation to converge toward the statistical
one before fading. *Horizon Zero Dawn* (Sheaf `#ABD2B8`): scale animation down
and push vertices down with distance.
