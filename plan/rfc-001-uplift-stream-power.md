# RFC-001: Uplift fields and stream power run to quasi-equilibrium

- Status: Draft
- Area: terrain simulation
- Interacts with: RFC-002 (diffusion), RFC-004 (performance), RFC-014
  (merge-tree hydrology)

## Problem

The generator composes noise first and erodes second.  The recipe blends
continent, plains, and ridged-mountain noise through a mask
(`make_geological_fields` in `moppe/terrain/geological.cc`), and only then
do droplets and talus modify the result.  Erosion is a garnish on shapes
that never earned their drainage: ridged noise produces mountain *texture*,
not mountain *structure*.  Valley networks are shallow and local, divides
sit wherever the noise put them, and no amount of droplet count buys the
dendritic coherence, uniform profile concavity, and knickpoint character
that real landscapes get from the interplay of uplift and incision.

## Current situation

- `AnalyticalErosion` (`moppe/terrain/analytical_erosion.cc`) already
  implements the finite-time n = 1 stream-power characteristic solution
  after Tzathas et al., with `uplift_rate` in metres per Julian year, a
  fixed sea level, per-iteration depression-aware drainage
  (`analyze_wet_drainage`), and relaxation.  It is applied as one transform
  over a noise-born terrain.
- The drainage machinery (receivers, ordered accumulation, priority flood)
  is deterministic and battle-tested (`drainage.cc`, `flood.cc`).
- The research shelf holds the relevant papers:
  `research/terrain/cordonnier-2016-uplift-fluvial-erosion.pdf`,
  `research/terrain/jain-2024-fastflow.pdf`,
  `research/terrain/barnes-2016-priority-flood.pdf`.

## Proposal

Invert the roles: reinterpret the geological recipe as an **uplift-rate
field** U(x) and generate relief by evolving

    dz/dt = U(x) - K * A(x)^m * S(x)^n + D * laplacian(z)

to quasi-equilibrium, with ocean cells as fixed base level.  The mountain
mask becomes where the crust rises fast; plains become slow-uplift regions;
the continent field can modulate erodibility K or uplift.  Mountains are
then *what erosion leaves of uplift*, and every ridge has a valley system
that explains it.

Use the Braun-Willett implicit solver for the incision term (n = 1): with
receivers computed, one ordered downstream-to-upstream sweep solves each
cell implicitly,

    z_i' = (z_i + dt * (U_i + K A_i^m z_rcv' / d_i)) / (1 + dt K A_i^m / d_i)

which is unconditionally stable, O(n) per step, deterministic, and exact
for arbitrary dt in the linear (n = 1) case.  Interleave the diffusion
term by operator splitting (RFC-002).

## Design

- New transform (or better, new *source mode*) `OrogenyEvolution` with
  physical parameters: total duration, dt, K, m, D, sea level, and an
  uplift field expressed as an ordinary `ScalarField` so the Lab can edit
  it with the existing recipe controls.
- Drainage recomputation: receivers change as the terrain evolves.
  Recompute every step at first; measure, then relax to every k steps.
  Depression routing reuses `FloodField::spill_receiver`; when RFC-014
  lands, the basin graph replaces repeated priority floods.
- Boundary conditions: ocean cells (largest submerged component, as in
  `flood.cc`) are Dirichlet-fixed at sea level.  The torus has no other
  boundary, which is exactly why the explicit-ocean convention already
  exists.
- Units: duration and dt in Julian years, U in `meters_per_julian_year_t`,
  K in yr^-1 * m^(1-2m) -- the mp-units vocabulary in
  `moppe/quantities.hh` already covers this.
- Typical regime: m ~ 0.5, n = 1, dt 10-100 kyr, 20-100 steps to
  near-steady-state.  Iterate at 1025^2 in the Lab; the play resolution
  runs at world generation.

## Consequences

- Dendritic valley networks with consistent concavity; divides, spurs,
  and knickpoints with geological legibility.
- The recipe acquires honest semantics: `mountain_weight` stops being an
  additive blend factor and becomes an uplift amplitude.
- Droplet erosion is demoted to a detailing pass (gully texture, alluvial
  fans) and the hero-droplet spectacle; its count can fall by an order of
  magnitude (see RFC-004).
- The Lab gains the best spectacle it could have: watch a mountain range
  grow against erosion in geological time (`ideas/terrain-lab-spectacles.md`
  is full of adjacent wishes).

## Risks and alternatives

- Character change: worlds will look different.  Keep the current blend
  path as a source mode; make orogeny a new option, compared side by side
  in the Lab before it becomes a profile default.
- Cost: priority flood per step is the bottleneck at 2049^2.  Mitigations:
  coarse-to-fine (evolve at 513^2/1025^2, refine with a short high-res
  tail), drainage every k steps, RFC-014's merge tree, or the FastFlow
  GPU formulation.
- Steady state can look *too* orderly.  Spatially varying K (from the
  plains noise) and finite (non-equilibrium) durations restore variety;
  both are single knobs.

## Implementation sketch

1. `StreamPowerEvolution` transform: implicit sweep given a receiver
   array; unit tests against the analytical pass on simple profiles.
2. Drainage-refresh loop with the existing flood/drainage readings;
   determinism test (bit-identical across runs and thread counts).
3. Uplift-field plumbing from the recipe; Lab source mode + knobs.
4. Calibration pass: pick K, dt, duration for each generation profile;
   compare against the current look with `terrain-pipeline-demo` images.
5. Only then: performance work per RFC-004/RFC-014.

## References

- Braun & Willett 2013, "A very efficient O(n), implicit and parallel
  method to solve the stream power equation."
- `research/terrain/cordonnier-2016-uplift-fluvial-erosion.pdf`
- `research/terrain/jain-2024-fastflow.pdf`
- `research/terrain/tzathas-2024-analytical-erosion.pdf` and the existing
  `moppe/terrain/analytical_erosion.cc`
