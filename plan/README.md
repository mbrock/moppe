# Plan: RFCs for the landscape, the simulation, and the semantics

Design RFCs distilled from a full-codebase review and design
conversation (July 2026).  Each file states a problem, the current
situation with code references, a proposal, consequences, risks, and an
implementation sketch.  Most remain drafts; an RFC records an implementation
date when its acceptance path lands.  Current architecture stays documented in
`docs/`; speculative essays stay in `ideas/` -- this directory is the
bridge between them: concrete enough to start, honest about cost.

## Landscape process (simulation)

- [RFC-001](rfc-001-uplift-stream-power.md) — Uplift fields + implicit
  stream power run to quasi-equilibrium: mountains as the residue of
  uplift under erosion. Implemented and selected for ordinary world
  generation on 2026-07-13.
- [RFC-002](rfc-002-hillslope-diffusion.md) — Hillslope diffusion (soil
  creep): smooth convex crests against sharp valley cuts.  Small,
  standalone, also RFC-001's D term.
- [RFC-003](rfc-003-sediment-ledger-rasters.md) — Keep the per-cell
  sediment ledger as eroded/deposited rasters; feed materials, detail,
  and Lab overlays.  Cheapest coherence win available.
- [RFC-004](rfc-004-erosion-performance.md) — The erosion performance
  path: wire the Metal field evaluator into world generation (found
  gap), counter-based droplet RNG, lockstep batches as a deterministic
  GPU kernel.

## Geometry and shading (rendering)

- [RFC-005](rfc-005-fragment-rate-normals.md) — Fragment-rate normals
  for distant terrain: decouple lighting detail from geometric LOD.
  Highest look-per-effort in the renderer.
- [RFC-006](rfc-006-analytic-channels.md) — River channels as analytic
  vector features composited at sample time, not raster carves.
  Sub-cell widths, clean banks, resolution independence.
- [RFC-007](rfc-007-waterline-conforming-geometry.md) — Extract the
  waterline curve and make water and terrain conform to it.  Kills the
  shoreline sawtooth class permanently.
- [RFC-008](rfc-008-geology-conditioned-tessellation.md) — Near-field
  tessellation displaced by the simulation's own history (ledger,
  flow direction, moisture), inside a bounded envelope.
- [RFC-009](rfc-009-relief-readings.md) — Cavity ambient occlusion and
  a fixed-azimuth horizon-angle map: soft exact terrain shadows at any
  sun height; candidate retirement of the 4096^2 shadow pass.
- [RFC-010](rfc-010-diagonal-bias.md) — Union-jack triangulation to
  remove the global diagonal grain from ridges and silhouettes.

## Semantic foundations

- [RFC-011](rfc-011-field-domain-remap.md) — Fields as functions over
  an explicit domain: `remap`/precomposition as the one warp operator,
  periodicity as a quotient domain, fBm as a derived combinator.
- [RFC-012](rfc-012-elevation-datums-normalization.md) — Elevation as a
  datum-anchored point; rasters carry their calibration; normalization
  split into a measurement and an affine reparameterization.
- [RFC-013](rfc-013-discretization-reconstruction.md) — A
  discretization is a lattice *plus a reconstruction*: name the four
  existing continuizations, state the gather/scatter adjunction once,
  declare the droplet model's lattice-unit regime.
- [RFC-014](rfc-014-merge-tree-hydrology.md) — The merge tree of the
  heightfield as the one hydrological structure: floods, lakes, spills,
  and sea-level queries as views of a single precomputation.

## Suggested sequencing

Compounding order, small-and-visible first:

1. **RFC-003 → RFC-005 → RFC-007**: three contained changes attacking
   three different visibility classes (material coherence, distant
   lighting, shorelines).  RFC-004 Tier A (evaluator wiring) rides
   along any time.
2. **RFC-001 + RFC-002**: the simulation rebuild that changes what the
   world *is*; RFC-014 supports it and independently unlocks the Lab's
   sea-level interactions.
3. **RFC-006 and RFC-008**: consume what 1-3 produce (ledger, waterline,
   equilibrium valleys).
4. **RFC-011/012/013** run in parallel as foundations: each is
   incremental, none blocks the visual track, and RFC-006 is the
   natural first client of RFC-013's vocabulary.

RFC-009 and RFC-010 are independent and can fill gaps between larger
efforts.
