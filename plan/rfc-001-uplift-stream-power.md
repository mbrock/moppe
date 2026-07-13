# RFC-001: Uplift fields and stream power run to quasi-equilibrium

- Status: Implemented (2026-07-13)
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

which is unconditionally stable, O(n) per step, deterministic, and an exact
solve of each backward-Euler step in the linear (n = 1) case.  It is not the
exact continuous-time solution for an arbitrarily long dt.  Interleave the
diffusion term by operator splitting (RFC-002).

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

1. [x] `StreamPowerEvolution`: implicit downstream-to-upstream sweep with
   closed-form profile tests and agreement with the analytical solution as dt
   decreases.
2. [x] Refresh standing water and wet drainage every step; preserve periodic
   seams, fixed ocean/base-level cells, deterministic repeatability, and the
   no-spurious-rise incision bound.
3. [x] Add typed `RelativeUpliftField` plumbing, a shallow-continent source
   mode, physical orogeny controls, progress, and convergence readings to the
   Lab.
4. [x] Interleave stable hillslope diffusion after every incision step and
   report uplift, incision, net volume, and last-step change separately.
5. [x] Calibrate `K = 2e-5`, `m = 0.4`, `D = 1e-4 m2/yr`, 1 mm/yr maximum
   uplift, and 50 kyr steps. Fast, Play, and Research durations are 200 kyr,
   500 kyr, and 1 Myr.
6. [x] Compare fixed seeds with `terrain-pipeline-demo` and measure the
   1025-square Research path. Details are recorded in
   `research/terrain/orogeny-evolution-experiment.md`.

## Implemented boundary

`make_orogeny_program` is an explicit alternative source program. The normal
world profiles still use the established relief-source pipeline, so the visual
change remains opt-in while it is evaluated in the Terrain Lab. The Lab's
Orogeny preset and `+OROG` stage expose duration, dt, uplift, K, m, D, and sea
level; dragging Age reruns the same uplift recipe for before/after comparison.

The implementation recomputes priority flood and routing every geological
step. RFC-014 now provides a merge-tree flood view, but not yet the dynamic
spill forest and wet-drainage replacement this solver needs. At 1025 square,
20 Research steps took about 6.2 seconds on the measured M2 Pro, so preserving
the correctness baseline is preferable to relaxing routing refresh. The
future performance work remains RFC-004/RFC-014 rather than a hidden k-step
approximation here.

## References

- Braun & Willett 2013, "A very efficient O(n), implicit and parallel
  method to solve the stream power equation."
- `research/terrain/cordonnier-2016-uplift-fluvial-erosion.pdf`
- `research/terrain/jain-2024-fastflow.pdf`
- `research/terrain/tzathas-2024-analytical-erosion.pdf` and the existing
  `moppe/terrain/analytical_erosion.cc`
