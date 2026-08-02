+++
id = "RTG-022"
title = "Measure and record the raw reflection verdict"
rfc = "RFC-0004"
track = "reflection-geometry"
status = "done"
depends_on = ["RTG-021"]
order = 70
areas = ["rendering", "metal", "measurement", "docs"]
+++
# Measure and record the raw reflection verdict

## Outcome

The repository says whether this raw signal earns composition or denoising.

## Scope

Count input, visible, and terrain-hit pixels; report all frame-slot targets;
measure the ray query without drawable pacing; inspect the raw panels; and run
the macOS and Apple-platform validation matrix.

## Acceptance

- Cost separates ray query, signal targets, and retained Goal 0 geometry.
- Signal coverage and terrain-hit share quantify the visual opportunity.
- The verdict can stop before composition even when traversal works.

## Evidence

The forcing case has 69.7% water coverage but only 0.9% terrain hits. Four
fresh 640x400 isolated M2 Pro queries have a 0.238 ms median. Goal 1 remains an
atelier; the next decision requires a more grazing forcing camera.
