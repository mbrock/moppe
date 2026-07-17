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

## What comes after the ground (not now)

A ladder, each rung small and imageable:

1. **Water as a datum** — a sea-level origin, an axis-inverting depth
   frame, shore materials from a typed distance-to-waterline reading.
2. **The first flow** — a signed 1-cochain of transport on the edges;
   accumulation as the tree's `accumulate_along_tree` generalized to
   the lattice's flow ordering.  (Drainage, but stated as calculus.)
3. **A datum family** — home site and spawn as heuristically placed,
   frame-guarded origins; the trail map becomes a chart at the home
   origin rather than arithmetic.
4. **Time and evolution** — sections indexed by epochs; the terrain
   history moppe keeps as raw snapshots becomes a typed sequence.
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
