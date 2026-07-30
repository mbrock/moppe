+++
id = "LIV-011"
title = "End the unison sway and glue crowns to the ground"
rfc = "RFC-0003"
track = "living-world"
status = "backlog"
depends_on = ["LIV-010"]
order = 70
areas = ["rendering", "vegetation"]
+++
# End the unison sway and glue crowns to the ground

## Outcome

Neighbouring trees sway out of phase with each other, lean relative to the
camera, and never float above or sink into the terrain.

## Scope

Three cheap changes that share one mechanism — per-object data the crown
geometry currently has nowhere to put, except that untextured crowns have two
unused float lanes:

- **Per-object phase offset**, so trees at the same world XZ stop moving
  identically.
- **Camera-based tilt**, which costs a transform and reads as volume from a
  low chase camera — the exact case it was designed for.
- **Ground hugging**: the crown base tracks the sampled height, which is
  nearly free since the vertex stage already pulls the height texture.

Also flip the double-sided foliage normal by absolute value in view space;
crowns currently light backwards from behind.

## Acceptance

- A still frame of a stand shows trees at different points in the gust.
- No crown floats or sinks on a slope or across an LOD boundary.

## Research

*Horizon Zero Dawn* vegetation (Sheaf `#ABD2B8`): index/offset lanes,
camera-based tilting, ground hugging.
