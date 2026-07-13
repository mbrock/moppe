# RFC-002: Hillslope diffusion (soil creep)

- Status: Draft
- Area: terrain simulation
- Interacts with: RFC-001 (as its diffusion term), usable standalone

## Problem

The transform vocabulary has advection (droplets, analytical stream power)
and thresholded talus (`erode_thermally`), but no linear diffusion.  Real
landscapes are shaped by the pairing of advective incision in channels and
diffusive soil creep on hillslopes: creep is what gives soil-mantled hills
their smooth, parabolic crests, in contrast to the sharp V-cuts of bedrock
gorges.  In Moppe, hilltop character currently comes from raw noise
octaves, so crests are as jagged as valleys and the landscape misses one
of the strongest "this is real terrain" signals there is.

## Current situation

- `RandomHeightMap::erode_thermally` (`moppe/map/generate.cc`) moves
  material only above a talus threshold -- a nonlinear, thresholded
  operator that produces scree, not creep.  Below the threshold nothing
  moves, so gentle convexities never round off.
- The transform pipeline (`docs/terrain-expressions.md`) classifies
  operations by `SpatialScope` and `EvaluationOrder`; diffusion is a
  `Neighborhood` / `Iterative` transform exactly like thermal erosion, so
  it drops into the existing machinery without new concepts.

## Proposal

Add a `HillslopeDiffusion` transform evaluating dz/dt = D * laplacian(z)
over the unique torus samples:

- Parameters: `duration` (Julian years) and `diffusivity` D (m^2 per
  Julian year) -- both physical, both trivially expressible with the
  existing mp-units vocabulary.
- Scheme: explicit 5-point Jacobi with the stability bound
  dt <= h^2 / (4 D); the transform derives a safe internal dt from its
  grid spacing and runs ceil(duration/dt) sweeps.  At landscape D values
  the sweep count is small.  (An ADI implicit variant is a contained
  upgrade if long durations ever need it.)
- Topology: wrap on the torus, clamp on bounded maps, matching the
  neighbor conventions in `drainage.cc`.
- Report: lowered/raised volume and mean/max change, mirroring
  `AnalyticalErosionReport`, so the Lab's Map Readings window can show it.

## Consequences

- Convex, smooth hilltops and ridge crests against sharp valley cuts --
  the classic advection/diffusion contrast, visible from the saddle.
- A gentler fix than talus for single-cell droplet spikes in soft areas;
  thermal remains for genuinely oversteep scree.
- Inside RFC-001's loop (operator splitting: incision sweep then diffusion
  sweep per step) it is the D term of the standard landscape-evolution
  equation, completing the model.
- GPU-friendly by construction: a 5-point stencil is the easiest possible
  compute-kernel port, and a natural second resident stage for the
  "keep results GPU-side" boundary in `docs/terrain-expressions.md`.

## Risks and alternatives

- Over-smoothing erases plains detail; D and duration are per-profile
  knobs, and the Lab's before/after Delta reading makes the tuning visible.
- Nonlinear (slope-dependent) creep models exist; not worth the
  complexity until the linear term has proven itself.

## Implementation sketch

1. `terrain::HillslopeDiffusion` value + kernel in `RandomHeightMap`
   (sibling of `erode_thermally`), unit test: a Gaussian bump spreads
   with the analytic variance growth; volume conserved to float epsilon.
2. Transform wiring: evaluator case, checkpoint replay, Lab stage row
   with knobs (`Continuous` domain for both parameters).
3. `terrain-pipeline-demo` keyword (`diffuse=duration,D`) for image
   comparisons.
