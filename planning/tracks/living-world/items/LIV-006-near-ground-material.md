+++
id = "LIV-006"
title = "Make the ground under the wheels bear looking at"
rfc = "RFC-0003"
track = "living-world"
status = "ready"
depends_on = ["LIV-001"]
order = 55
areas = ["rendering", "terrain", "materials"]
+++
# Make the ground under the wheels bear looking at

## Outcome

The ground within twenty metres of the bike reads as a surface rather than as
a field of coloured static.

## Scope

LIV-001 made the material bands work, which means the ground is now visible
from the saddle for the first time — and at close range it is a chaotic
per-texel mix of orange-brown and dark green. A four-times zoom of a spawn
capture shows no structure at all: not grain, not clumping, not anything a
material does. Just two very different textures interleaved at texel scale.

Three things compound, and the item should establish which dominates before
changing any of them:

- **The source textures carry too much per-texel variance.** `grass3.tga` and
  `dirt.tga` are each strongly variegated, and the blend interleaves them.
- **Each layer then multiplies itself by another texture sample.** Grass by a
  luminance at one twelfth the frequency, scree by a red channel at one
  sixteenth. Both were tuned when the ground was mostly snow and nobody was
  looking at this.
- **The detail normal is driven by the composed texel's own luminance**, so a
  noisy colour becomes a noisy normal and the lighting amplifies the noise it
  was given.

The fix is likely to be material authoring rather than shading: a ground
texture wants low-frequency variation and fine grain, not full-range contrast
at every texel. But the cheapest experiment is to fade the self-multiplication
terms and the detail normal to nothing under a few metres and see what is left.

Note that this is where full-geometry grass would eventually live
(`ideas/geometry-from-fields.md` item 1). Blades occlude the soil, which is
the real solution and a much larger one. This item is about the ground being
acceptable before that exists, not about replacing it.

## Acceptance

- A spawn capture at four-times zoom shows a material with structure.
- The trail still reads as a trail against it.
- No regression in the terrain block of the graphics benchmark.

## Research

*Between Tech and Art: The Vegetation of Horizon Zero Dawn* (Sheaf `#ABD2B8`)
on coverage-preserving mip chains; the general point that a ground material's
job at one metre is different from its job at fifty.
