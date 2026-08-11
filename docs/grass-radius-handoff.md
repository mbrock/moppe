# Handoff: the grass representation frontier

Updated 2026-08-11 after reproducing the low-resolution failure. This note
supersedes the earlier fixed-radius proposal in this file.

The player's complaint is not that too few grass objects are drawn far away.
It is that the world changes register around the camera: a dense, vertical,
moving medium becomes a flat dark material. Moving the line outward, widening
surviving blades, or scattering coarse tufts only moves or decorates the
failure. The game includes walking, jumping, riding, and gliding, so a solution
that works only for a near-horizontal chase camera is not a solution.

## What the neutral reproduction shows

Use a cross-lit frame with bloom, exposure adaptation, flare, shafts, and
motion blur disabled. Those effects are part of the finished image, but they
hide the representation boundary while diagnosing it.

At a 957x538 scene resolution the grass-lab cross-lit view shows a nearly
straight frontier between upright blades and the dark far register. The same
view at about 1600x900 moves the frontier outward because projected feature
size is larger. The downward view and an aerial view are required companions:
a ground-level frame alone can make a camera-centred answer look acceptable.

The reproducible low-resolution glide is:

```sh
MOPPE_GRASS_LAB=1 MOPPE_UPLIFT_YEARS=0 \
MOPPE_TERRAIN_PROFILE=smoke \
MOPPE_GLIDE=grass-gradient-crosslit MOPPE_GLIDE_FRAMES=120 \
MOPPE_GAZETTEER_GRAPHICS=high MOPPE_GAZETTEER_WINDOW=1600x900 \
MOPPE_GAZETTEER_DISABLE=bloom,auto-exposure,lens-flare,light-shafts,motion-blur \
MOPPE_RENDERSCALE=0.35 \
  tools/capture-terrain-gazetteer /tmp/grass-neutral-glide
```

`MOPPE_GAZETTEER_DISABLE` is recorded in `capture.txt`; it is a diagnostic
condition, not a proposed finished-game preset.

## The discarded answers

### A world-distance floor for explicit blades

The previous version proposed keeping blade geometry full to 40 m and fading
it by 80 m. That would make one windowed chase view less conspicuous, but it
would make subpixel geometry authoritative and would still produce a ring when
viewed from another resolution or altitude. It was not wired in and has been
removed.

### Coarse far tufts

A short experiment drew a second population of larger grass cards after the
fine population retired. Neutral frames immediately showed what it was: green
dabs occupying the missing register. It preserved neither the continuity of a
field nor the intended quotient from population to medium. The experiment was
discarded.

### Raising the terrain

Commit `e93cf51` raised terrain vertices by a camera-distance-dependent sward
height. That gave a flat material some parallax, but made the authoritative
ground appear to move as the camera moved, disagreed with physics, and supplied
incorrect motion vectors because the previous frame reused the current
camera-dependent displacement. RFC-0006 explicitly forbids this. The
displacement is removed.

## The actual model

Grass has three evaluations of one semantic medium:

1. resolved blades where individual leaf width is repeatable;
2. a terrain-following canopy surface for the resolvable vertical and optical
   extent of an unresolved population; and
3. an integrated optical material when even that extent is subpixel.

The middle evaluation is a field, not another set of individuals. Metal
samples it as world-anchored patches only to bound dispatch. Adjacent patches
evaluate identical world-space edge coordinates, so their gluing condition is
exact. The surface reads the same terrain height, habitat, leaf-area-derived
cover, clumping, tint, flowering wash, grain cascade, lighting limit, and gust
field as the other two evaluations. Its height is the complement of the
resolved-blade weight and returns to zero when the whole sward height is
subpixel. It sits above the true terrain and never changes physics.

This is now implemented as `sward_canopy_*` in
`moppe/shaders/metal/undergrowth.metal`. It is a Gate 3 proof, not a claim that
the complete grass medium is finished. In particular, rootability and basal
cover remain incomplete, and the far optical response still needs calibration
against the blade ensemble in motion.

Current evidence covers the 957x538 and 1634x919 cross-lit grass-lab views, the
steep downward view, a real-world translating meadow pass, and a 60-frame
autopilot ride. The neutral frames still expose a dark far register; the proof
has supplied the missing field-valued vertical rung, but has not earned a claim
that the complete optical transition is finished. Two alternating 32-case GPU
benchmark runs at a 1280x720 scene put the undergrowth block median at about
3.10 ms before and 3.14 ms after. Treat the roughly 0.04 ms difference as
within run spread, not as evidence that the surface is free or faster.

## A separate camera bug found in the reproduction

Grass and forest LOD used `abs(view_proj[1][1])` as focal scale. That is valid
for a projection matrix, but not for a projection already multiplied by the
view rotation: pitching or yawing changes that diagonal. The result was LOD
that changed merely because the player looked down or turned.

The shaders now recover horizontal and vertical projection scale from row
norms of the unjittered world-to-clip matrix. An orthonormal view rotation
preserves those norms, so walking, jumping, riding, and gliding use the same
projected-size law at the same true distance and resolution.

## Acceptance evidence, not a scalar target

The texture and motion profilers remain useful instruments, but they cannot
decide whether the world feels continuous. A contrast peak can score higher
when the artifact is worse, and motion compensation can cancel a constant
state error. The required order is:

1. neutral wide looks at low and high scene resolution;
2. the same medium seen cross-lit, sunward, antisun, and steeply down;
3. a translating ground-level glide and a low aerial pass over a real habitat
   boundary;
4. actual walking, jumping, riding, and gliding in the game; and
5. only then profiles and GPU timings to locate regressions and price the
   representation.

No individual still, profile scalar, or ground-level camera circle is an
acceptance test.
