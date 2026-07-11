# Hydrology frontier

Where the water work stands and where it goes next. This is the active
battlefront; read `theory-of-the-world.md` first for the concepts.

## State

- **Sediment ledger closed.** Droplets terminate by natural water
  death (evaporation below threshold; measured mean life ≈ 305 steps,
  matching the theoretical ~300 from the 1.5% evaporation
  constant). Residual sediment settles explicitly. Ledger displays
  ERODED = DEPOSITED, LOST 0.0%, and a death census (water cutoff /
  flat / boundary / step cap). Keep it balanced; keep it visible.
- **Lifetime hypothesis tested and split.** Longer droplet lives
  dramatically reduce mass loss and roughly double maximum drainage
  integration, but do *not* cure the sink explosion (~2.3–2.8k sinks
  at 513² regardless of lifetime, vs 929 uneroded). Conclusion stands
  in two parts: conservation was one disease (fixed); pit-minting is
  another (open), prime suspect the four-cell bilinear erosion
  footprint undercutting the droplet's own path. That suspect has now
  been tried and partly convicted: path-monotone carving reduces sinks
  and small water bodies substantially without eliminating large lakes.
- **FloodField exists** (`flood.cc`): deterministic priority-flood
  from sea-level seeds (global-minimum fallback), torus-wrapped,
  8-connected, yielding water level `w`, depth, a spill-receiver
  forest, and outlet seeds. Note the explicit caveat: the forest is
  *not yet* the depression-aware drainage graph, and `outlets` are
  seeds, not per-lake classified spills. Do not let that
  interpretation become accidental.
- **Lake census exists.** `census_lakes` labels deterministic
  eight-connected wet components on the unique periodic domain and
  records cell count, physical area, maximum depth, volume, mean
  surface elevation, a boundary spill candidate, and a
  puddle/pond/lake/sea class. Component ids follow first cell in scan
  order. Sea classification currently means a body's mean surface is
  sea level; it is not yet proof of ocean connectivity.
- **Permanence exists as a reading and render policy.** `WATER` shows
  every positive flood depth. `LAKES` and gameplay use
  `WaterPermanence`: at present an inland body must clear 600 m² area,
  0.25 m maximum depth, and 100 m³ volume. Bodies below that remain in
  the census. The ocean shader retains a 4 cm fragment cutoff only as
  a final anti-z-fighting tolerance.
- **Path-monotone carving exists.** `CarvingRule` makes the choice
  explicit. `Unconstrained` retains the former footprint behavior;
  `PathMonotone` prevents each affected sample, including pending
  batch deltas, from falling below the downstream handoff elevation.
  On the recorded 257² trial it changed sinks 636 → 416, puddles
  384 → 207, and ponds 83 → 40 while preserving all three lakes and
  the closed sediment ledger. This is a deliberately strong first
  law; clamp-frequency instrumentation is still missing.

## Sequenced plan (agreed)

1. **Finish lake identity.** The deterministic BFS census and coarse
   classes are implemented. Add mean depth, robust ocean connectivity,
   an exact per-body spill contract, a lake inspector, and size-
   distribution export. Promote `WaterPermanence` from a C++ value
   with defaults into editable/serializable terrain-program data.
   Expect a heavy-tailed size distribution, but measure it rather than
   assuming it.
2. **Preserve the ephemeral layer.** Sub-threshold hollows already
   remain data; retain them for future rain and splash physics.
   Puddles are a protected gameplay class (splashable), ponds are
   jumpable boundaries, and lakes are routable obstacles. Rendering
   permanence and physical wetness should remain distinct policies.
3. **Finish the pit-minting audit.** The before/after census supports
   path monotonicity, but attribution is not yet implemented. Add per-batch
   last-writer drop ids on touched cells; diff minima per batch;
   attribute new minima (on own trajectory → footprint suspect;
   downstream of fresh positive delta → deposition dam; neither →
   unnamed bucket, the valuable one). Measure pit *lifetimes* —
   steady-state survivors matter, not birth rate.
4. **Refine path-monotone carving.** The current node-wise floor is
   stronger than the originally proposed flowline-only constraint.
   Count clamp activations and compare it with the weakest sufficient
   interpolated constraint; a constantly binding clamp indicates a
   calibration problem upstream, not a solved pathology.
5. **Depression-aware routing.** Route flow using the flood surface:
   descend on dry `z`, enter lakes, cross equal-`w` interiors without
   cycles, and exit at the classified spill carrying the
   basin's full discharge. FLOW/STREAMS/BASINS overlays stop lying;
   black-hole termini disappear. This is still observation—geometry
   untouched. Do not equate the existing priority-flood parent forest
   with this graph until the routing contract and accumulation order
   are named and tested.
6. **Lake infill and spillway incision.** The graduation: couple
   erosion to the flood surface. Drops entering standing water lose
   slope, deposit (deltas; ponds silt into flats — the
   kettle-pond-to-bog-to-meadow biography), and relay their authority
   to the outlet; spill cells incise under the basin's discharge,
   lowering lake levels, retreating shorelines, letting the network
   grow across former lakebed. Order matters: 5 before 6, or spillways
   incise with a fraction of their true discharge. After this stage
   the sink/lake census over erosion time should finally crest and
   fall, converging to the endorheic elect.

## Reference points

Beyer 2015 (the droplet model's lineage); Braun & Willett 2013 (O(n)
implicit stream power along the receiver tree; geological age as a
real parameter); Cordonnier et al. 2016 (uplift + fluvial for
graphics); Jain et al. 2024 *FastFlow* (GPU flow routing O(log n),
depression routing O(log² n) via basin-graph MST; escarpment demo
shows exactly our trapped-water disease and its cure); Tzathas et
al. 2024 (analytical stream power via method of characteristics;
erosion as upstream-traveling news; AGE as the radius of the
boundary's influence; terrain beyond the wavefront remembers its
initial state). Their pathologies rhyme with ours: independent river
trees leave cliffs at basin boundaries (our batch septa at solver
scale), corrected slowly because borders move one cell per iteration
(the speed-of-news limit again), cured by multigrid (squint first so
news travels fast, focus later).

## The long arc

Bones from stream power (with uplift and age as world dials),
standing-water truth maintained throughout, conservation-closed
monotonic droplets as finishing texture, and the census instruments
watching the world's biography: sinks up through adolescence, over the
crest, down to the honest inland seas. Then the water hands off to the
second author — spill points and confluences are centers-in-waiting,
valley networks are roads-in-waiting, and the lake at the bottom of
the world is where every argument ends.
