# Multitudes

*Notes toward giving the world's many things — stars, trees, lakes,
vehicles, cells of cloth — the same storage discipline the terrain
already has.*

## Where the terrain bundle lives, for the record

The lattice side of the world has answered its ownership questions, and
`docs/generated-world.md` writes them down: `GeneratedWorld` is the one
non-copyable, non-movable owner of the surface bundle, the hydrology,
and the readings; a loading worker builds a complete candidate; the main
thread transfers the owner exactly once at activation; sessions borrow
the surface and release their borrows before a retired world is
destroyed; a staged table says which artifact is valid after which build
step. Copies happen at known, nameable moments: the solver seeds its
working `ElevationMap` from the input span, the cache loads from Arrow,
presentation packs columns into texture lanes. The remaining shadows are
the solvers' interior double-buffers — real bundles, embodying the whole
landscape mid-thought, invisible from outside the call — and those are
the storage the buffer-royalty direction would raise into caller-owned
workspaces.

So the uneasy feeling is not really about the terrain. It is about
everything else.

## The inventory of the many

The world's populations currently live in bespoke containers, one
idiom per kind:

- `Stars` keeps `std::vector<Star>`, each star a struct of position,
  phase, and collection state.
- The forest keeps `ForestSite` vectors inside distance-sorted chunks,
  beside a procedural hash-based covering that is not stored at all.
- `TreeStand` keeps its own `TreeSite` vector.
- Lakes are the hydrology's `WaterBody` vector plus a per-cell
  `WaterBodyId` column — half population, half field.
- The river network keeps reaches and confluences as indexed vectors.
- The Atelier hex sheet keeps cells that split and grow, with an
  adjacency rebuilt from intrinsic geometry.

Each of these is fine alone. Together they are a scattering: no shared
way to iterate, inspect, serialize, upload, or account for a
population. The question "where is the state of the world?" has one
good answer for fields — in the bundles, in the world's owner — and
eight small answers for things.

## What a population is, in this codebase's own terms

`docs/ontology.md` already names the concept: a population is many
instances of one universal in one context, with genidentity — being
*the same star* across birth, death, and slot reuse. The prolegomena
of nxtrt (the sibling runtime project) names the storage topology: a
**hive**, organized by occupancy, where identity survives movement and
completion — as against a **ring**, organized by succession. The
terrain lattice is neither: it is a *field* domain, where sites are
places and nothing is ever born.

What the spatial layer has today is the field case only. `Bundle`
abstracts beautifully over *which lattice*; it does not yet know about
domains whose sites appear and disappear. That is the missing sibling:

**a population domain** — a finite domain in the `spatial::` sense
whose index type is a generational handle, whose sites are slots with
occupancy, and whose `sites()` view walks the living. Columns over it
are components; a population is then simply a bundle:

```
Stars      = Bundle<StarDomain,  star_position, star_phase, ...>
TreeStand  = Bundle<TreeDomain,  tree_position, tree_height, ...>
WaterBodies= Bundle<LakeDomain,  spill_elevation, lake_area, ...>
```

The three prototypes already in the tree say this is not speculative.
The hex sheet is a dynamic domain in all but name — cells split, the
domain grows, bundles over `HexSheetTopology` follow. The flood's
water bodies are a population keyed by `WaterBodyId` with a per-cell
membership field — exactly the fiat-object projection the ontology
document describes, waiting for its domain type. And nxtrt's `farm` is
the allocator this domain wants: slots plus a feed of free indices,
occupancy as a hierarchical mask, allocation you can await.

## What the uniform treatment buys

The point is not tidiness; it is that every capability the lattice
bundles earned transfers to the many at once.

*Visibility.* One registry of domains — the lattice plus each
population — answers "where is everything" with a walkable structure
instead of a code search. The world's owner becomes a chart of
domains the way it is already a chart of readings.

*Inspection and the central view.* A population bundle is rows and
typed columns: the debug HUD, the future ledger, and any report can
render one the way they render any table. Births and deaths post to a
population's account (a `pacioli` pair per population: spawned //
retired — the census as a T-account, turnover distinguishing a quiet
population from a churning one at equal size).

*Serialization.* `spatial::bundle_storage` already writes bundles as
Arrow. A population that is a bundle checkpoints with the terrain in
the same file format, which is most of what replay-complete world
state needs and half of what a save file is.

*The GPU.* Columns are contiguous and typed; instanced rendering wants
exactly that. The forest's per-chunk site vectors are hand-rolled
instance buffers; population columns would be the same bytes with a
name, a spec, and a schema.

*Correctness.* Genidentity bugs — a recycled slot resurrecting the
wrong star — become type-level concerns of one domain implementation
instead of a per-system convention. And the field/population boundary
gets honest: `WaterBodyId` per cell is a *field* whose values are
handles into a *population*, and saying so in types is the projection
discipline (`fields → fiat objects`) the ontology essay asks for.

## Costs, honestly

Dynamic domains break two comforts of the lattice case. Offsets are no
longer dense forever — iteration must skip the dead or compact them,
and either choice shows up in the neighbourhood and storage code.
Domain equality — which `join` leans on — becomes identity-plus-epoch
rather than structural comparison. And the interpolation/neighbourhood
machinery mostly does not apply: populations are not metric lattices,
and should not pretend to be (their spatial queries go through the
*fields* they project into, or through an index bundle like the forest
chunks, not through a fake adjacency).

The hex sheet is the proof that growth can be honest in this design;
it is also the warning about how much care adjacency-under-change
takes. Populations are easier: most have no intrinsic adjacency at
all.

## Where to begin, when it begins

Stars. The smallest population, the simplest lifecycle (spawn at
generation, collect, respawn), no adjacency, already rendered as
instances. One `PopulationDomain` with generational handles, one
bundle with three or four columns, one account posting births and
collections. If the shape is right there, the forest's site chunks
and the water bodies follow, and the hex sheet can eventually say
what it always was.

None of this obliges any refactoring, in the tradition of the
ontology document: it is here so that when a new many-of-a-kind
arrives — traffic, wildlife herds, weather cells — the discussion can
be about what population it is, not about which container idiom to
copy this time.
