# The Atelier earth

A proposal to widen the atelier's scope: from a studio of small organisms
(the tree, the carpet, the cellular sheet) to a second implementation of
the engine itself, begun again from the foundations of the earth.

This is not a refactor of moppe and not a port.  Moppe continues to run,
generate, and play.  The atelier grows a world beside it, small and
whole at every stage, made of the semantic material we have learned to
want: typed quantities, affine frames, rank-graded domains, sections,
and a discrete calculus — with Metal as the prime instance substrate.
Where moppe discovered these ideas mid-flight and carries them partially,
the atelier bakes them in from the first line.  The two meet later by
adoption, organ by organ, never by conversion.

## What "engine" means here

Not "motorcycle game."  The engine is a simulation of a world:

- a **combinatorial storey** — finite topologies: lattices, trees,
  sheets; vertices, edges, faces; incidence and orientation.  No
  positions live here.
- an **intrinsic storey** — typed sections over those topologies:
  bundles whose columns are labelled by mp-units quantity specifications.
  Elevation is a point-valued 0-cochain; a flow is a signed 1-cochain;
  an area measure is a 2-cochain.  The calculus (coboundary, transport,
  accumulation, Laplacians) acts here and is checked by the type system.
- an **extrinsic storey** — embeddings and presentations: charts into
  the metric stage of `space.hh`, meshes, materials, cameras, capture.
  An embedding may never alter topology or intrinsic sections.

The tree already lives by this doctrine (docs/atelier-tree.md).  The
earth is the same doctrine applied to the ground under everything.

## The creed

1. **Sentences become types.**  "A 1 km by 1 km terrain sampled on a
   4096-square lattice with bilinear interpolation, closed as a torus"
   is a value of a type, not a comment.  If a sentence at that semantic
   level cannot be written down directly, the vocabulary is missing and
   the vocabulary is the work.

2. **Quantities everywhere; numbers only past the bridge.**  Lengths in
   metres, angles in the strong angular system, points distinct from
   displacements.  `in_metres` and its siblings remain the only doors
   through which a number leaves its unit, and they open only toward
   the GPU.

3. **Heuristics compute datums; types guard frames.**  Sea level, the
   home site, a camera anchor: found by search, held as origins.
   Conversions between frames are registered projections
   (`mp_units::frame_projection`), both directions explicit, runtime
   data passed openly as arguments.  On a torus the world-to-home
   projection *is* the choice of chart, and the wrap lives in exactly
   one place.

4. **Topology is a policy, not an if.**  The atelier's sheet already
   knows `periodic | open`.  The earth keeps that: a periodic boundary
   wraps (`wrap_to_range` on the normalized chart), an open boundary
   clamps.  No call site ever re-implements the wrap.

5. **The atlas is a deliverable.**  Every domain, section, origin, and
   projection is enumerable, and the enumeration is kept as a document
   beside the code.  A reader should be able to ask "what kinds of
   thing exist in this world, and over what?" and receive a table.

6. **Capture first.**  Every visible thing has a deterministic headless
   capture from day one, in the moppe tradition (`--capture PATH N`).
   The proof of a stage is a picture that can be diffed.

## The first work: the ground

One bounded ambition, finished before anything else begins:

> Render a bilinear heightmap over a toroidal square lattice as a
> repeating flat Euclidean mesh, with normals and a material gradient.

Small, complete, and visibly itself — like the tree.  Concretely, the
piece introduces one domain family, a handful of sections, and one
presentation, at this semantic level:

```c++
// The plat: the sentence "1 km × 1 km on a 4096² lattice, torus"
// written as a value.
inline constexpr GroundPlat home_plat {
  .extent   = { 1.0f * si::kilo<si::metre>, 1.0f * si::kilo<si::metre> },
  .lattice  = { 4096, 4096 },
  .boundary = SheetBoundary::periodic,
};

Ground ground (home_plat);
auto& height = get<terrain_height> (ground.vertices ());   // 0-cochain
auto  slope  = coboundary (ground, height);                // 1-cochain
```

### The combinatorial storey: `GroundTopology`

A square lattice with explicit **vertex, edge, and face domains** and
the incidence between them.  This is the piece moppe never made
explicit — its flow arcs, facets, and cells all exist, but folded into
structs.  Here the ranks are first-class from the start:

- `GroundVertices` — the lattice sites.
- `GroundEdges` — oriented horizontal and vertical links (two per
  vertex on the torus).  Signed values over these are 1-cochains.
- `GroundFaces` — the square cells (one per vertex on the torus).

Periodic closure means no seam vertices and no duplicated rows: the
torus is the honest object, and the seam belongs to presentation.  The
domains satisfy the atelier `FiniteDomain`/neighbourhood concepts, so
the existing `Bundle`, folds, and Laplacian apply unchanged.

### The intrinsic storey: sections and the first calculus

- `terrain_height` — a `quantity_point` 0-cochain over vertices, in the
  elevation frame (its origin is the ground datum; sea level arrives
  later as a projection, not as a float threaded through calls).
- `coboundary` — the first genuine operator: heights to signed
  per-edge rises (`Length`-valued 1-cochain).  Dividing an edge's rise
  by its typed run gives slope; the units make the difference between
  the integrated form (metres per edge) and the intensive form
  (dimensionless slope) impossible to confuse.
- `vertex_normal` — derived from the edge rises, not from neighbouring
  height taps scattered through rendering code.  The normal is the
  calculus made visible.
- `ground_material` — the first material gradient: a dimensionless
  blend driven by typed readings (elevation band, slope), evaluated per
  vertex and interpolated.  It exists to prove that materials are
  *functions of sections*, the pattern moppe's `Surface` grew toward.
- **Bilinear reconstruction** — `sample<terrain_height> (ground, at)`
  through the face containing `at`, with the affine algebra choosing
  anchor-plus-differences for points, exactly as the bundle already
  does.  The interpolation stencil is owned by the domain; the wrap is
  owned by the boundary policy.

The initial height content is whatever is pleasant to look at —
analytic swells in the spirit of the carpet are enough.  Terrain
*generation* is deliberately out of scope for the first work; the
ground must not wait for geology.

### The extrinsic storey: rendering the universal cover

A torus has no edge, so the presentation shows its universal cover: the
fundamental mesh, instanced in a neighbourhood of the camera's cell, so
riding east forever simply works.  The chart from lattice coordinates
to the metric stage is one typed function; the 3×3 (or distance-driven)
repetition is a presentation decision with no echo in the model.

Mechanically this is the atelier's existing Metal substrate
(`MetalExecution`, `RenderSurface`, MSAA, capture targets) plus the one
technique worth importing from moppe's renderer wholesale: **vertex
pulling from a height texture** (R32F heights, RG16Snorm normals),
which has already proven itself at 4096². The GPU bridge widens by
exactly one clause: a bundle column may materialize into a texture.
That clause — *sections become textures at the bridge* — is the
substrate story in one line, and it is why "Metal as prime instance"
does not leak upward: another substrate would materialize the same
sections differently.

Run modes, in the house style:

```sh
./build/atelier.app/Contents/MacOS/atelier --ground
./build/atelier.app/Contents/MacOS/atelier --ground --capture /tmp/ground.png 7
```

## What the atelier harvests from moppe

Learning, not carrying:

- **The quantity ontology** — the spec-as-treaty pattern from
  `quantities.hh`, `map/surface.hh`, and `terrain/fractional_drainage.hh`;
  specs like `snow_support` and `channel_tangent` are prior art for how
  fine the kinds should be cut.
- **The affine index experiment** — `terrain/discretization.hh`'s typed
  row/column/sample spaces, adopted where they pay rent.
- **Bundle experience** — `spatial/bundle.hh` and `atelier/bundle.hh`
  are siblings; the earth work continues the atelier line and feeds
  improvements back where they generalize.
- **Renderer knowledge** — vertex pulling, reversed-Z, the capture
  discipline, the deterministic-benchmark habit.
- **mp-units machinery** — `frame_projection` with runtime arguments,
  overflow policies, bounded points; the geographic example as the
  production pattern for wrapped coordinates.

And the merge posture: when an atelier organ (the ground, a future
hydrology) becomes clearly better than its moppe counterpart, moppe may
adopt it as a library — the same way it links `atelier_botany` today.
No flag day, no rewrite-in-place.

## The second work: the hexagonal commons

> Grow an equal-area hexagonal partition of the torus under the
> pressure of relief.

A height field does something to geometry that the flat lattice
ignores.  The graph embedding pulls a metric back onto the plane, and
the area element becomes `sqrt(1 + |grad h|^2) dx dz`: steep country
literally contains more landscape per unit of map than flat country.
That density is itself an object of the calculus — a face 2-cochain,
the induced area measure divided by the flat one — and it is the
"pressure" of this work's motto.

Over the same base as the ground, a second combinatorial storey grows:
a hexagonal partition whose cells divide wherever their quota of
landscape area is exceeded, and merge again where the country calms.
The tiling ends up equal-measure in the world's own metric — tiles of
the same amount of *place*, not the same amount of *map*.  Where the
mountains bunch the cloth, the tiling runs fine; across a plain, one
calm hexagon suffices.

The torus is the right home for this: with Euler characteristic zero
it admits a defect-free all-hexagon tiling — no pentagon tax, unlike a
sphere.  When adaptive division breaks regularity, the irregularity
*means* relief instead of paying topology.

The mechanism is largely already alive in the studio.  The cellular
sheet has the periodic boundary, the binary division with lineage, and
the rule that adjacency is rebuilt from intrinsic geometry so small
cells naturally meet large neighbours.  The new work is a coupling,
not a subdivision engine:

- `material_demand` — a dimensionless face section on the commons: a
  tile's integrated induced area over its footprint, divided by the
  area quota.  A cell whose demand exceeds one divides, along the axis
  that best splits its demand; a family whose joint demand falls well
  under one merges.  Relaxation is pressure equalization — tiles
  jostling like froth under the induced density until quotas agree.
- The chart and the commons are joined by one typed arrow: sampling
  the ground's sections at hex sites, and integrating them over hex
  footprints.  Two domains over one base, one projection between them.
- The partition's dual is a triangulation — simultaneously the natural
  render mesh and exactly the Voronoi–Delaunay pairing that keeps the
  discrete Hodge star well-behaved.  The tiling wanted for *meaning*
  is the mesh wanted for *math and Metal*; that coincidence is the
  tell that the object is right.

Equal measure is what makes the commons a commons: per-tile integrals
mean the same thing everywhere.  Uniform rain is one unit per tile;
budgets — vegetation instances, texel density, simulation particles —
become flat allocations; transport balances become legible at a
glance.

### Geology in commons space

The commons is not only a way to *draw* terrain; it is the space to
*simulate* it in.  Run uplift and erosion on the living partition and
the scheme becomes variable-bit-rate geomorphology — resolution spent
the way an audio codec spends bits, on the loud passages:

- The demand field widens beyond static relief to *activity*: uplift
  rate, incision rate, ice flux.  An orogeny is a loud passage and
  recruits fine tiles while it plays; a quiet craton coasts at low
  resolution for a hundred epochs; when activity fades, tiles merge
  and the region is re-encoded cheaply.  Division and merger are the
  codec.
- Events are local spikes of demand with their own material: a
  volcanic edifice arrives as new rock the partition must grow tiles
  to carry; an ice sheet is another stratum with its own rheology and
  its own melt.  The framework does not distinguish "terrain" from
  "event" — both are material demanding tiles.
- Each tile carries a stratified column — bedrock, regolith,
  sediment, lava, ice — a surface-voxel manifold rather than a bare
  height.  Moppe's sediment ledger (its eroded/deposited channels) is
  the flat, two-layer ancestor of this; the commons gives the ledger
  a body.  Height becomes a *derived* reading: the top of the column.
- Traced through time, the partition is a worldsheet: a 2+1
  spacetime complex in which divisions and mergers are events, and a
  tile's lineage is its geological biography.  The terrain history
  moppe keeps as raw snapshot vectors becomes a structured object —
  and old mountains remain readable as fine-grained scar tissue in
  the tiling long after erosion has calmed them.

The wager of the second work, stated plainly: adaptivity in the
world's own measure is not an optimization of the simulation; it is
the honest form of it.

## What comes after (not now)

A ladder, each rung small and imageable:

1. **Water as a datum** — a sea-level origin, an axis-inverting depth
   frame, shore materials from a typed distance-to-waterline reading.
2. **The first flow** — a signed 1-cochain of transport, inheriting
   the commons: six equidistant neighbours with no diagonal ambiguity
   make the hex partition the natural substrate for flow, where the
   square lattice's eight directions were always a managed bias.
   Accumulation as the tree's `accumulate_along_tree` generalized to
   the flow ordering.  (Drainage, but stated as calculus.)
3. **A datum family** — home site and spawn as heuristically placed,
   frame-guarded origins; the trail map becomes a chart at the home
   origin rather than arithmetic.
4. **Geological time** — the commons run as variable-bit-rate
   geomorphology above: uplift, erosion, stratified columns, events.
5. **An inhabitant** — something moving on the ground, reading it only
   through sampled sections.

Each rung ends in a capture.

## Non-goals of the first work

Gameplay, vehicles, HUDs, sound, terrain generation pipelines, erosion,
caching, loading screens, iOS.  All of these have homes in moppe and
will have better homes in the atelier later.  The ground owes them
nothing yet.

## The measure of success

The first work is done when a newcomer can read `atelier/ground.hh` in
one sitting, see the sentence "1 km × 1 km, 4096², bilinear, torus"
written as a value, watch `--ground` draw a landscape that repeats
without a seam, and diff the capture against a golden image — and when
the atlas can list, in one table, every domain, section, origin, and
projection the earth so far contains.

The second work is done when a capture shows the same landscape wearing
its commons — fine tiles clinging to the ridges, broad tiles resting on
the plains — and the histogram of per-tile landscape area is narrow.
