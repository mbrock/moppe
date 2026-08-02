# Reflection geometry

This track contains Goals 0 and 1 beneath Gate 3 of
[RFC-0004](../../rfcs/0004-reconstruct-expensive-pixels-on-metal-4.md). It
first proves the representation a water-reflection pass needs, then produces
one raw signal from the game's actual standing-water surface. Neither goal
changes ordinary water composition.

```mermaid
flowchart LR
  RTG001["RTG-001: freeze the forcing contract"]
  RTG010["RTG-010: build the terrain proxy"]
  RTG011["RTG-011: query and expose raw hits"]
  RTG012["RTG-012: measure and decide"]
  RTG020["RTG-020: freeze the raw-signal contract"]
  RTG021["RTG-021: raster inputs and trace reflections"]
  RTG022["RTG-022: measure and decide"]
  RTG001 --> RTG010 --> RTG011 --> RTG012
  RTG012 --> RTG020 --> RTG021 --> RTG022
```

Goal 1 traces one closest-hit reflection ray from a quarter-linear-resolution
standing-water input and keeps origin, optical normal, raw radiance, hit
normal, hit distance, and validity independently inspectable. It deliberately
does not composite the signal, accumulate history, denoise, interpolate
frames, trace running water, or add scene instances.

The track is complete. Both stages remain opt-in behind
`MOPPE_REFLECTION_GEOMETRY` and `MOPPE_WATER_REFLECTION_SIGNAL`. Goal 1 proves
a coherent, inexpensive raw query, but the forcing view contains too little
terrain-hit reflection to justify composition or denoising yet. See the
[findings](findings.md) for the proxy error, signal coverage, memory, timing,
hardware boundary, validation matrix, and scoped verdict.
