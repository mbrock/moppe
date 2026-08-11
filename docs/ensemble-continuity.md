# One medium, many resolutions: the ensemble-continuity doctrine

Distilled from the 2026-08-11 flora sessions, where every visible failure
and every fix turned out to be an instance of one structure. This note
states the structure so the next system (and the next debugging session)
can start from it instead of rediscovering it.

## The situation

The ground flora is generated, not stored: continuous habitat fields
(`grass_medium.h`, the surface readings) are read fresh every frame, and
plants are evaluations of those fields. On top of that sit three layers
of machinery built this session:

- a **family table** — a family is a way of spending one shoot's fixed
  vertex allowance, and families partition habitat (sward, fern rosettes
  in damp shade, flowering drifts in open meadow);
- a **per-family LOD ladder** — resolved individuals, then ensemble
  geometry, then substrate, each family descending by the projected size
  of its own signature feature (blade width, head diameter, frond
  width), with the camera window demoted to a cost bound;
- an **ensemble-limit substrate** — grassy terrain is lit by
  `moppe_sward_ensemble_light`, the blade fragment's own formula
  evaluated at its resolvable→0 limits over the same shared tint.

## The core idea

A representation switch is a change of theory. The blade shader is a
microscopic theory of grass; the tuft band is a coarse-grained theory of
blades; the substrate is the thermodynamic limit — what an infinite
field of unresolvable blades presents to one pixel. LOD is therefore a
renormalization tower, and **continuity is not a blending problem, it is
a matching-conditions problem**: at every rung boundary, the coarse
theory must reproduce the conserved moments of the fine one.

The moments are concrete and there are only a few:

1. **Mean (zeroth moment): energy and colour.** The coarse rung's mean
   lit colour must equal the fine rung's ensemble mean. This was the
   green-disc-on-brown-world bug: the substrate was a separately
   authored material that merely *resembled* grass, and it drifted. The
   fix was structural, not tuned — both rungs now evaluate one formula,
   so the mean matches *by construction* and cannot regress silently.
2. **Coverage (first moment): where the stuff is.** Retiring
   individuals must hand their projected area to something — surviving
   neighbours (tuft widening on the sqrt count curve) or the substrate
   (cover, drift wash). The mid-field pop-in was a first-moment leak:
   count fell and nothing absorbed the difference.
3. **Variance (second moment): texture.** Sub-resolvable *variance*
   must collapse to the ensemble mean before its carrier retires
   (exposure settling, chroma collapse, flutter fade, glint widening),
   and the coarse rung must carry the variance the fine rung still
   resolves (the sward's two grain octaves — currently ad hoc, see
   below). The camouflage-blob regression was a second-moment gap: the
   substrate had the right mean and no spectrum.
4. **Dynamics: the motion spectrum.** Blades gust; the substrate is
   static. This moment is currently UNMATCHED at the last rung — a far
   field that does not move is the remaining tell that it is paint. The
   shared `moppe_grass_gust`/`moppe_grass_ensemble_axis` exist precisely
   so the substrate can carry the ensemble's motion as a normal/sheen
   perturbation.

Two disciplines ride on top:

- **Perceptual gates.** Every descent is triggered by projected pixels,
  never metres, so the whole tower is resolution-aware for free.
- **One instrument per moment.** The verification failures of this
  session map exactly onto the moments: the texture profiler measures
  the second moment and was blind to a zeroth-moment bug; the motion
  residual measures dynamics and cancels state errors by construction;
  only the naive wide look sees the zeroth moment. A claim about a rung
  boundary is only supported by the instrument of the moment it is
  about — and the naive look precedes them all (working-practices.md).

## Where each matching condition stands

| Boundary | mean | coverage | variance | dynamics |
|---|---|---|---|---|
| blades → tufts | shared formula ✓ | widening ✓ | settling ✓ | gust kept ✓ |
| tufts → substrate | ensemble limit ✓ | cover ✓ | ad hoc grain ~ | MISSING |
| heads → wash | shared chromaticity ✓ | wash ✓ | footprint floor ✓ | rigid heads ✓ |
| fronds → ? | no far rung (window-culled) | — | — | — |
| window edge | hard cull — should be a budget fade | | | |

## What the doctrine predicts and demands next

- **The forest tint band has the grass-brown bug.** Terrain colours
  forested ground with `ground_value * (0.48, 0.72, 0.34)` — a
  separately authored resemblance, exactly the class of zeroth-moment
  mismatch the sward just escaped. Prediction: distant canopy colour
  does not match what crown proxies present, and the seam is visible
  from the glider. The fix is the same contract: a
  `moppe_canopy_ensemble_light` shared with the forest fragment shader.
- **Close the loop: calibrate rung N+1 from rung N's measurements.**
  The sward grain octaves are hand-picked. But the texture profiler can
  *measure* the near field's variance-versus-distance curve, and the
  grain parameters could be fit so the substrate *continues that curve*
  — instruments graduating from verification to calibration. The same
  applies to the dynamics moment once substrate motion exists: fit the
  sheen-wave amplitude so the glide's temporal spectrum is continuous
  across the last rung.
- **The window edge becomes a fade in the budget**, not a cull — the
  one place a first-moment discontinuity survives by construction.
- **Ferns need a far rung** — a rosette is resolvable far beyond the
  window; its ensemble limit is a small dark-green disc in the
  substrate, cheap to add through the same medium.

## Why this generalizes

Nothing above is about grass. The forest already lives by pieces of it
(organ-by-organ arrival spreads error below notice; the crown proxy is a
bough ensemble), terrain LOD morphs are a coverage-moment device, and
water sheets face the same far-field questions. The doctrine in one
sentence: **the medium is the authority; every representation is that
medium evaluated at some resolvable scale; a scale boundary is correct
when the conserved moments match; and each moment has its own
instrument, behind the naive look.**
