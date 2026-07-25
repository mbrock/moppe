# RFC-015: Graphs as domains — typed maps, forests, and discovered domains

- Status: Draft
- Area: semantic foundations (terrain analysis representation)
- Interacts with: RFC-013 (discretization vocabulary), RFC-014 (merge
  tree as the one hydrological structure); realizes the three-storey
  reading of `ideas/the-machine.md` and the certificate distinction of
  `ideas/computed-together.md` for the hydrology stack

## Problem

Vertex-associated world data has a first-class home: a typed column in
a `spatial::Bundle` over a `FiniteDomain`, where a quantity can only
exist at a site of its domain.  Relational structure — which cell
drains to which, which cell belongs to which lake, which reach feeds
which — has no such home.  It lives in bare index vectors whose meaning
exists only in the loops that consume them.  Three symptoms:

1. **Receiver forests are untyped.**  `DrainageGraph::receiver`,
   `WetDrainageRouting::receiver`, `FloodField::spill_receiver`,
   `MergeTreeNode::parent`, and `RiverReach::downstream_reach` are all
   the same thing — a map from a domain to itself whose functional
   graph is a forest — stored as `std::vector` of raw indices.
   Nothing ties an index column to the domain it points into, so
   indexing the wrong array with a `CellIndex` compiles.  The load-
   bearing invariant, acyclicity, is checked nowhere or discovered
   mid-pass (`"wet drainage routing contains a cycle"`,
   `moppe/terrain/drainage.cc:553`; the hand-rolled cycle detection in
   the flow-distance solver, `moppe/terrain/drainage.cc:339`).

2. **The topological order is computed and thrown away, three times.**
   Contributing-area accumulation is one algorithm — a fold over the
   forest in donor-first order — implemented thrice with different
   order witnesses: descending-elevation sort
   (`moppe/terrain/drainage.cc:403`), Kahn's algorithm with a
   determinism queue (`moppe/terrain/drainage.cc:526`), and Kahn again
   for fractional routing
   (`moppe/terrain/fractional_drainage.cc:315`).  Only the fractional
   path keeps its order (`FractionalFlowDomain::topological_order`),
   which is why `stream_power_evolution.cc` can run its closed-form
   reverse sweep.  The dry and wet analyses discard theirs, so every
   later consumer re-derives structure by pointer-chasing.

3. **Discovered domains are hand-rolled parallel arrays.**  The flood
   analysis discovers the water bodies; the census stores that as
   `body` (a cell→body map) plus `bodies` (per-body fields) with no
   named body domain.  River extraction does the same with
   `reach_by_cell` + `reaches`; the merge tree with `cell_node` +
   `nodes`.  Because "column over bodies" has no type, per-body data
   lands in whatever aggregate is handy:
   `RiverNetwork::body_traversed` (`moppe/terrain/drainage.hh:158`) is
   a boolean column over the body domain living inside the river
   network.

In the vocabulary of `docs/ontology.md`: these are grids whose
projection is unchecked.  The two classic representation bugs — a cell
projecting onto nothing (dangling index) and cell structure
disagreeing with object structure (wrong-domain indexing) — are
exactly the bug classes the current spellings permit.

## Current situation

The good news is that the target design already exists in the codebase
once, fully formed.  `FractionalFlowDomain`
(`moppe/terrain/fractional_drainage.hh:167`) is a graph *as a domain*:
it satisfies `FiniteDomain`, exposes structure through visitors rather
than bare arrays, carries its topological order as a construction-time
certificate, and `FractionalDrainage` is a `Bundle` over it.  This RFC
is mostly "do for the rest of the analyses what the fractional domain
already does," plus one new generic layer so the pattern stops being
bespoke.

What exists and stays:

- `spatial::FiniteDomain` needs only `size`/`offset`/`index`, so a
  trivial enumerated domain already satisfies it.
- `TerrainCellDomain` owns the lattice neighbourhood visitors; stencil
  structure stays implicit in lattice domains (it is structural, not
  data — the one cleanup is that drainage, flood, and merge-tree code
  each re-declare the 8-neighbour offsets locally).
- Row-structs `WaterBody` and `RiverReach` stay row-structs.  The
  essential move is naming the domain so maps in and out of it are
  typed, not columnarizing small tables.

## Proposal

Three small types in `moppe/spatial/`, then a staged retyping of the
hydrology stack onto them.  Physical layouts do not change; every new
type is a named wrapper over the vectors the code already builds.

### 1. Enumerated domains for discovered index sets

    template <typename Tag>
    struct EnumeratedDomain {         // satisfies FiniteDomain
      using index_type = Id<Tag>;     // strong id, sentinel for "none"
      std::size_t count;
    };

with aliases `WaterBodyDomain`, `RiverReachDomain`, `MergeNodeDomain`
in `moppe/terrain/`.  An analysis that discovers a partition returns
its domain explicitly instead of implying it by a vector's length.

### 2. `Map<From, To>`: the one relation type

    template <FiniteDomain From, FiniteDomain To>
    class Map {                        // column of To-indices over From
      From m_from; To m_to;
      std::vector<typename To::index_type> m_targets;
    };

Total and partial variants (partial keeps the existing sentinel
convention — `no_cell`, `no_water_body` — but typed).  Construction
checks that the column length matches `from.size()`; lookups return
indices guaranteed to lie in `to`.  This single type covers the
quotient maps (cell→body, cell→reach, cell→merge-node), the structural
maps (body→outlet-cell, body→spill-cell), and the linkage maps
(reach→downstream-reach).  It is a Bundle column whose value type is
an index — relations get the same citizenship as quantities.

### 3. `Forest<D>`: an endo-map carrying its certificate

    template <FiniteDomain D>
    class Forest {
      Map<D, D> m_receiver;               // fixed points are roots
      std::vector<index_type> m_order;    // donors before receivers
    };

Constructed only by algorithms that produce a valid order, so
acyclicity is a constructor invariant rather than a mid-pass
exception.  Two named constructors matching the two witnesses the
code already uses:

- `Forest::by_potential(map, potential)` — sort by strictly
  decreasing potential (the dry analysis; elevation is the potential).
- `Forest::by_indegree(map)` — Kahn's algorithm with the
  deterministic min-queue (the wet analysis); a cycle throws *here*,
  at construction, with the offending cell.

Over a forest, the recurring passes become three generic algorithms:

    accumulate_downstream(forest, column, combine)
        // donors-first fold: the contributing-area solve
    fold_from_roots(forest, column, combine)
        // reverse-order sweep: flow distance, stream-power incision
    contract_chains(forest, eligible)
        // -> (EnumeratedDomain, PartialMap<D, that>, chain paths)
        // the reach-extraction quotient

The order certificate serves both directions: forward is the
accumulation, reverse is the downstream-first sweep
`stream_power_evolution.cc` already performs.  (Per
`ideas/computed-together.md`: the *sequence* is machine-answerable —
any linear extension would do — but its existence certifies the
world fact, acyclicity.  The type promises the fact, not the
particular extension; determinism of the extension is a separate,
already-honoured convention.)

Weighted routing (`FractionalFlowDomain`) stays as it is in this RFC —
it is already the exemplar.  A later stage may split
`FractionalFlowRoute`'s three roles (structure, operator weights, edge
geometry) into arc columns so `spatial::get` works over arcs and the
`FractionalRouteBackend` contract narrows, but nothing below depends
on that.

## Consequences

- Wrong-domain indexing and dangling indices become type errors;
  cycles become construction errors with context instead of
  mid-accumulation `logic_error`s.
- Three accumulation loops (~40 lines each) collapse into one
  algorithm; the memoized flow-distance solver with its cycle guard
  becomes `fold_from_roots` over the reach forest.  The same
  vocabulary then serves every level of the quotient tower: cells,
  bodies, reaches, merge nodes.
- `LakeCensus` becomes `WaterBodyDomain` + `PartialMap<cells, bodies>`
  + the existing `WaterBody` rows; `body_traversed` moves to a column
  over the body domain where it belongs.
- The dry `DrainageGraph` finally keeps its order, unlocking the same
  single-pass consumers the fractional domain enjoys, and giving any
  future threaded sweep its schedule for free (depth-grouping a
  carried order is a colouring).
- The code aligns with the three-storey account the essays give:
  combinatorial topology (`Forest`, `Map`, domains), intrinsic bundles
  over it (existing), derived embeddings (`reach.cells`, alignments —
  explicitly non-authoritative unfoldings).

## Risks and alternatives

- **Determinism goldens.**  Results must stay bit-identical: the
  constructors must reuse the exact current order computations
  (`stable_sort` by elevation; the `std::greater` min-queue), not
  merely equivalent ones.  This is the acceptance gate for stage 1.
- **Abstraction creep.**  The temptation to build a general graph
  library should be resisted.  Every graph here has out-degree ≤ 2
  with meaningful slots; `Map`/`Forest` plus the fractional domain
  cover all of them.  No edge lists, no CSR, no adjacency queries
  beyond what visitors already provide.
- **Wrapper cost.**  `Map` and `Forest` are layout-identical to the
  vectors they replace; passes stay flat loops over spans.  Any
  measurable regression in `analyze_drainage` or the wet accumulation
  (both profiled zones) blocks the stage that caused it.
- **Alternative considered:** widening `BundleValue` so bundles hold
  index columns directly.  Rejected: a relation needs its *target*
  domain checked, which a quantity column cannot express; `Map` is
  the honest type.

## Implementation sketch

1. `spatial/map.hh` + `spatial/forest.hh` (`EnumeratedDomain`, `Map`,
   `Forest`, the three algorithms), with unit tests, including the
   cycle-at-construction case.
2. Retype `DrainageGraph::receiver` as `Forest<TerrainCellDomain>` via
   `by_potential`; port the dry accumulation to
   `accumulate_downstream`.  Gate: existing drainage tests and a
   fixed-seed world checksum unchanged.
3. Retype the wet path: `route_wet_drainage` returns the routing map,
   `analyze_wet_drainage` builds `Forest::by_indegree` and reuses the
   same accumulation.  Delete the in-pass cycle exception.
4. Introduce `WaterBodyDomain`; retype `LakeCensus` and move
   `body_traversed` onto it.  Mechanical change with many call sites —
   keep it a single commit with no behavior change.
5. Introduce `RiverReachDomain`; retype `reach_by_cell` and
   `downstream_reach`; replace the flow-distance solver with
   `fold_from_roots`.
6. Optional follow-ups, each independently droppable: reach extraction
   via `contract_chains`; merge-tree nodes as an enumerated domain
   (coordinates with RFC-014); arc columns and the narrowed backend
   contract in fractional drainage; hoisting the thrice-declared
   8-neighbour stencil onto the lattice domain.
