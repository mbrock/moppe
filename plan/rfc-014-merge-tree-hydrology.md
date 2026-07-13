# RFC-014: The merge tree as the one hydrological data structure

- Status: Draft
- Area: terrain analysis (hydrology), enabling simulation and Lab work
- Interacts with: RFC-001 (depression routing per step), Lab sea-level
  interaction, storm/spectacle ideas
  (`ideas/terrain-lab-spectacles.md`, `geometry-from-fields.md` item 8)

## Problem

Every standing-water question is currently answered by re-running a
priority flood at one fixed sea level.  `analyze_standing_water` floods
the whole map to produce one `FloodField`; `census_lakes` labels its
bodies; spills are recovered by walking receiver chains; the endorheic
and all-land cases need explicit special handling (`flood.cc:70,155`).
Change the sea level -- or ask "what if this basin filled?" -- and
everything recomputes from scratch.  RFC-001 would need depression
routing *every timestep*, and the Lab cannot offer a continuous
sea-level slider or a "watch the basins fill" spectacle at interactive
cost.

The underlying structure is well known: sinks are minima of the height
function, spills are saddles, lakes are connected components of
sublevel sets, and the way components merge as the water rises is the
**merge tree** of the heightfield.  The priority flood computes one
horizontal slice of it; the census re-derives fragments of it; the
Cordonnier basin graph (on the research shelf, and cited by the erosion
docs) *is* it.

## Current situation

- `analyze_standing_water`: deterministic priority flood over the
  unique torus, minimax water levels, spill receivers, explicit global
  ocean by largest-submerged-component (`moppe/terrain/flood.cc`).
- `census_lakes`: BFS labeling of wet bodies, per-body area / depth /
  volume / spill via receiver-chain walk, with a documented subtlety
  about flats crossed by the flood path (`flood.cc:299`).
- `analyze_wet_drainage` re-routes flat lake interiors per body
  (`drainage.cc:215`).
- All deterministic, all correct, all single-sea-level.

## Proposal

Compute the merge tree once per terrain and derive the rest as views.

### Construction

Sort unique cells by height (the sort drainage already performs);
sweep upward with union-find.  A new cell either starts a component
(it is a local minimum: a tree leaf, born at its height) or joins /
merges components (a merge node: a saddle, recording the saddle cell
and height).  On the torus this needs no boundary cases at all -- the
sweep is topology-blind given the neighbor function.  Ties break by
cell index, preserving the codebase's determinism-by-construction
convention.  Cost: the existing sort plus near-linear union-find;
memory: one node per minimum plus one per merge, tiny beside the
rasters.  Store per node: birth height, merge height, saddle cell,
representative minimum, and per-node accumulated cell count / volume
integrals (enabling O(1) area/volume queries at any level by
interpolation along the tree edge).

### Derived views

- **FloodField at level L**: cut the tree at L; each surviving subtree
  below the cut is a body; water level per cell = min(L, its subtree's
  spill chain), reproducing the minimax surface.  The "global ocean" is
  the subtree containing the largest below-L component -- the same
  deterministic choice as today, now a tree query.
- **LakeCensus at level L**: the nodes alive at L, with area / depth /
  volume read from the accumulated integrals; spill = the saddle where
  the node's subtree merges outward.
- **Depression routing for erosion** (RFC-001): the basin graph per
  timestep is the tree's saddle structure restricted to current
  receivers -- Cordonnier's linking, without a per-step flood.
- **Continuous sea level in the Lab**: a slider samples the same tree;
  "storms fill the drainage network" and tide/fill spectacles animate
  one scalar against a static structure.
- **Waterfall persistence**: drops along reaches can be ranked by how
  long they survive as sea level / carving vary -- a principled
  significance measure if waterfall selection ever needs one.

## Consequences

- One O(n log n) precomputation replaces per-query floods; sea-level
  queries become O(tree) or better.
- The endorheic and all-land cases stop being special: an all-land
  torus is simply a tree whose root never meets the ocean, and the
  explicit fallback choice (global minimum) is the root's
  representative -- the current behavior, derived instead of coded.
- Erosion evolution (RFC-001) loses its most expensive per-step
  component.

## Risks and alternatives

- The terrain *changes* under erosion, invalidating the tree; per-step
  rebuild costs the sort.  Mitigations: rebuild every k steps (heights
  change slowly per step), or incremental merge-tree maintenance
  (known but fiddly) -- measure before choosing.
- The existing flood/census code is proven and subtle; keep it as the
  differential-testing oracle (bit-compare FloodField-from-tree against
  `analyze_standing_water` across seeds and levels) and only then swap
  call sites.
- Waves/epsilon wetness (`wet_epsilon`) live above this layer,
  unchanged.

## Implementation sketch

1. `terrain::MergeTree` builder + oracle tests against `flood.cc` at
   the canonical sea level over many seeds (including all-land and
   near-all-ocean synthetics).
2. FloodField/census adapters; Lab sea-level slider as the first
   consumer (it is pure win and exercises every query path).
3. RFC-001 integration; then decide the incremental-maintenance
   question with profiler data.

## References

- `research/terrain/barnes-2016-priority-flood.pdf`
- `research/terrain/cordonnier-2016-uplift-fluvial-erosion.pdf`
- Carr, Snoeyink & Axen 2003, "Computing contour trees in all
  dimensions" (the standard construction this specializes).
