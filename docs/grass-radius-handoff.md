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
2. a terrain-following density-column evaluation for the resolvable vertical
   and optical extent of an unresolved population; and
3. an integrated optical material when even that extent is subpixel.

The middle evaluation is a field, not another set of individuals. Metal uses
a conservative top envelope, sampled as world-anchored patches only to bound
dispatch. Adjacent patches evaluate identical world-space edge coordinates,
so their gluing condition is exact. From that entry envelope, the fragment
shader follows the view ray toward the local ground and integrates four
samples of a continuous three-dimensional density field. The optical path is
capped by the medium's finite lateral correlation length, so a grazing view
does not turn an infinite mathematical sheet into black paint.

The column reads the same terrain height, habitat, leaf area, basal cover,
clumping, canopy height, tint, flowering wash, grain cascade, leaf-normal
distribution, lighting limit, and gust field as the other evaluations. Its
weight is the complement of resolved blades and returns to zero when the
whole sward height is subpixel. It sits above the true terrain and never
changes physics.

This is now implemented as `sward_canopy_*` in
`moppe/shaders/metal/undergrowth.metal`. It is a Gate 3 and Gate 4 proof, not a
claim that the complete grass medium is finished. Rootability remains absent;
the basal layer remains visually shallow; and the bounded optical response
still needs calibration against the blade ensemble in longer motion.

The last resolved blades do not run that aggregate response again per
fragment. Their own tint, hemispherical fill, directional term, thin-leaf
transmission, and glint converge to the shared first moments as blade width
becomes unrepeatable. The density column beneath them owns aggregate coverage
and texture. This is both the correct ownership boundary and materially
cheaper than blending every blade toward a second full material evaluation.

Current evidence covers 957x538 and 1640x922 cross-lit grass-lab views, the
steep downward view, a 166-frame translating meadow pass, ordinary meadow,
trail, wetland, eroded-slope and aerial sites, and a 120-frame neutral
autopilot ride. The former bright-blade/dark-ground circle is gone. A lower
frequency aggregate remains visible where individual width and then total
height become subpixel; that is a necessary loss of bandwidth, not a license
for a camera-centred material change. Longer interactive traversal acceptance
remains open.

The first six-sample density implementation measured 6.33 ms for the standard
undergrowth block and was rejected. Per-pass isolation showed that recomputing
the complete aggregate material on every retiring blade was the dominant
error, not the density column itself. Four density samples plus in-place
moment convergence brought the short pass-timed profile to 3.88 ms. The final
full 32-case run measured 3.85 ms median and 4.82 ms p95 for the undergrowth
block, versus about 3.10 ms median across the two pre-density runs. Every
configuration remained below the 60 Hz deadline; the new representation is
not free, and its roughly 0.75 ms median price must be compared as a
distribution rather than used to overrule a visible transition.

One color-space bug mattered more than several rounds of lighting tuning. The
grass photograph is sampled as sRGB and therefore arrives in linear light,
but the old far path divided its luma by the display-space mean `0.40`. The
measured linear mean of the shipped texture is `0.0902`. Normalizing both the
column and far material against that value removed most of the dark register
without inventing extra light.

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
