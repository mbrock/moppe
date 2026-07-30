+++
id = "LIV-013"
title = "Let the ground remember the last few seconds of riding"
rfc = "RFC-0003"
track = "living-world"
status = "backlog"
depends_on = ["LIV-011"]
order = 90
areas = ["rendering", "vegetation", "gameplay"]
+++
# Let the ground remember the last few seconds of riding

## Outcome

Vegetation flattens where the bike passes and stands back up slowly.

## Scope

A small camera-following world-space texture. Wheel contacts splat into it;
it decays at a constant rate; the vegetation vertex stage samples it and
biases toward a downward flatten. No per-blade physics.

The mechanism that matters is *persistence*, not collision: the Responsive
Grass paper stores collision strength per blade and uses it to suppress the
recovery force, so a blade that was hit hard stays down longer. A decaying
scalar field reproduces the perceptual result for a texture fetch.

This is deliberately the short-memory version of `geometry-from-fields.md`'s
desire paths. The long-memory version — a persistent wear field that the
terrain shader darkens into a track and that eventually feeds back into grip —
belongs to movement 4, and this item should choose a representation that can
grow into it rather than one that cannot.

## Acceptance

- Riding through a stand leaves a visible line that recovers over seconds.
- Cost is a texture and a fetch; no measurable frame regression.

## Research

*Responsive Real-Time Grass Rendering* (Sheaf `#PQ68ZH`): collision strength
with linear decay, suppressing the recovery force.
