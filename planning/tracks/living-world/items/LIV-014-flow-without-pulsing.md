+++
id = "LIV-014"
title = "Stop the water surface pulsing and let speed show"
rfc = "RFC-0003"
track = "living-world"
status = "ready"
depends_on = []
order = 100
areas = ["rendering", "water"]
+++
# Stop the water surface pulsing and let speed show

## Outcome

River and lake detail advects along the flow without the whole surface
breathing in lockstep, and a still pool looks glassy where a chute looks rough.

## Scope

Moppe advects two-phase detail along the alignment already. Two ingredients are
missing, and both are a handful of instructions:

- A noise fetch that decorrelates the phase per pixel. Without it, every
  fragment resets at the same moment and the surface pulses — the classic
  failure mode, named as such by the technique's own authors.
- Normal strength scaled by flow speed in tangent space. Moppe knows velocity
  from discharge, width, and depth, so this is free, and it separates still
  water from moving water with one multiply.

Offsetting the two layers relative to each other kills the remaining
repetition.

## Acceptance

- A held camera on a reach shows no global pulse.
- A pool and a chute in the same frame read as different water.

## Research

*Water Flow in Portal 2* (Sheaf `#A2QB8L`): the two named failure modes,
repetition and pulsing, and their fixes; speed-scaled tangent-space normals.
Measured cost in its original form: two texture fetches, twenty-one pixel
instructions.
