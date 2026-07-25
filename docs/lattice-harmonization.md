# Lattice harmonization

The game is a couple dozen named fields on one lattice, plus a motorcycle.
The seamless-torus and authoritative-surface passes removed the two largest
historical splits: duplicated seam storage and the 2008 height-map hierarchy.
This document records what landed and the smaller harmonization work that
remains.

## Decisions

- **The world is a torus.**  `Topology::Bounded` and every `periodic ()`
  conditional are deleted, not preserved.
- **Storage is the unique lattice.**  The duplicated seam row/column dies.
  A 2048-resolution torus stores 2048x2048 samples; spacing = extent/2048.
  Wrap is owned by the sampling boundary: CPU interpolation wraps indices,
  GPU shaders wrap texel fetches with period = texture size.
- Default resolutions become powers of two: 2048 (play/research), 1024
  (fast, lab captures).
- Test fixtures convert once, to the final semantics: an NxN map is N
  periodic cells, spacing = size/N.

## Stage: seamless torus (done)

Touch points, surveyed:

- `terrain/topology.hh` — Topology enum deleted; carriers drop the field
  (`TerrainGrid`, `TerrainDiscretization`, recipes, `WorldParams`,
  `SurfaceDomain`).
- `TerrainGrid.unique_width/height/size` — deleted; there is only
  width/height and their product.
- The former `map/generate` storage: scale = size/width; `periodic ()` and
  `in_bounds` clamping deleted, interpolation wraps the +1 neighbor;
  `synchronize_periodic_edges` and the ledger twin deleted;
  `compute_normal_map` keeps only the wrapped branch, minus seam writes;
  cache format unchanged in shape (dimensions now seam-free; build-id keying
  invalidates old files).
- `map/TerrainEvaluator` + `terrain/cpu_evaluator` + Metal evolution
  backend — align `field_sampling_grid` and evolution grids with
  storage == unique (check orogeny.metal for width assumptions).
- `map/surface.cc` — `expand_unique_cells` becomes identity and dies with
  the span primitives it fed; `surface_coordinate` always wraps.
- Metal `terrain.metal` — already wraps with period = size-1; period
  becomes size, `periodic` uniform dies.
- WGSL (`webgpu_renderer.cc`) — `sample_position` clamps today; becomes
  the same modulo wrap; `smooth_height`'s continuous clamp becomes wrap.
- `game/terrain.cc` — chunks_per_side = width/CHUNK; edge chunk bound
  scans wrap their +1 reads.
- `game.cc` — `world.toroidal ()` always true (ocean offset
  unconditional); `WorldParams.terrain_topology` deleted; default
  resolution 2048, fast 1024.
- Terrain Lab / water capture / tests — fixture conversions and
  re-derived expectations (surface stencil boundary tests now wrap).

Landed in "The world is a seamless torus": 67 files, net -347 lines.
Generated worlds were preserved bit-for-bit -- the old convention
sampled unique cells at k/(storage-1), which equals the new k/N with
the resolution dropped by one.  Web/WGSL is compile-only verified from
this machine; run the web build before trusting the browser path.

The deletion follow-up removed the remaining constant-periodic branches,
identity seam expansions, and renderer topology uniforms. Analysis and surface
lattices must now match instead of being silently tiled through modulo
indexing. Terrain Lab still prefers routes away from its current chart cut;
that is an explicit presentation constraint, not a second topology.

## Stage: authoritative surface bundle (done)

`SurfaceAtlas::geometry()` is the one finite terrain store. Its mandatory
typed columns are elevation in metres, normal, removed material, deposited
material, and broad snow support. Elevation is an affine quantity point with
the native representation of a `float`; it is neither boxed nor normalized.
`GeneratedWorld` owns one `Surface`; there is no preceding height map and no
geometry refresh copy.

Generation, Terrain Lab checkpoints, physics, hydrology, and rendering all
read or mutate those columns. `TerrainView` is a borrowed analysis adapter,
not another allocation. The renderer accepts typed elevation and normal spans
directly; compile-time layout checks guarantee their representations are the
native float and `Vec3` payloads expected by the GPU upload boundary.

Deleted with the old layer:

- `HeightMap`, `NormalComputingHeightMap`, `RandomHeightMap`, `NormalMap`, and
  `Array2D`;
- `moppe/map/generate.hh` and `generate.cc`;
- raw height/normal/ledger pointers;
- `Surface::refresh` and the copied geometry materialization barrier.

## Stage: physical elevation (in progress)

The authoritative geometry column, CPU reconstruction, hydrology, terrain
evolution, trails, and water sheets now share one physical elevation frame in
metres. The old vertical scale survives only as a temporary presentation
calibration: the existing terrain and water shaders still consume normalized
texture lanes, so their renderer upload boundaries perform that conversion.

Compatibility methods named `relative_elevation` and the corresponding
`TerrainGrid::height_scale` remain while test fixtures and analysis entry
points move to typed bundles. They are migration scaffolding, not part of the
intended model.

## Stage: direct finite geology (done)

The continent/uplift expression compiler was much more general than its live
job. It has been replaced by `generate_geology`, which directly materializes
typed continent-shape and uplift-weight columns over `TerrainDomain`.
Generation retains the exact seeded periodic-noise arithmetic, deterministic
parallel row evaluation, and loading progress, without an intermediate DAG or
untyped raster.

Deleted with the interchange:

- `ScalarField`, its typed phantom wrapper, and the generic field algebra;
- `CpuEvaluator` and the virtual `FieldEvaluator` boundary;
- the Metal 4 function-stitching evaluator, shader ABI, and field shader;
- `TerrainDiscretization` and its separate field-sampling grid;
- tests for expression compilation and backends that production no longer
  used.

This pass removed 3,261 net lines. `TerrainDomain` is now the shared finite
domain for the surface and geological bundles.

## Remaining stages

1. **Finish physical elevation** — migrate fixtures and analysis APIs, delete
   relative-elevation access and vertical scaling, then let shaders consume
   metre-valued elevations directly.
2. **Finish one lattice value** — move remaining analysis results from
   `TerrainGrid`/`TerrainView` adapters onto `TerrainDomain` and bundle
   concepts.
