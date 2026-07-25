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
- Default resolutions become powers of two: 2048 (play/research), 1024 (fast).
- Test fixtures convert once, to the final semantics: an NxN map is N
  periodic cells, spacing = size/N.

## Stage: seamless torus (done)

Touch points, surveyed:

- `terrain/topology.hh` — Topology enum deleted; carriers drop the field
  (`TerrainGrid`, `TerrainDiscretization`, recipes, `WorldParams`, and the
  former `SurfaceDomain`).
- `TerrainGrid.unique_width/height/size` — deleted; there is only
  width/height and their product.
- The former `map/generate` storage: scale = size/width; `periodic ()` and
  `in_bounds` clamping deleted, interpolation wraps the +1 neighbor;
  `synchronize_periodic_edges` and the ledger twin deleted;
  `compute_normal_map` keeps only the wrapped branch, minus seam writes;
  cache format unchanged in shape (dimensions now seam-free; build-id keying
  invalidates old files).
- The former `map/TerrainEvaluator`, `terrain/cpu_evaluator`, and Metal
  evolution backend — align field sampling and evolution grids with
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
- The former Terrain Lab, water capture, and tests — fixture conversions and
  re-derived expectations (surface stencil boundary tests now wrap).

Landed in "The world is a seamless torus": 67 files, net -347 lines.
Generated worlds were preserved bit-for-bit -- the old convention
sampled unique cells at k/(storage-1), which equals the new k/N with
the resolution dropped by one.  Web/WGSL is compile-only verified from
this machine; run the web build before trusting the browser path.

The deletion follow-up removed the remaining constant-periodic branches,
identity seam expansions, and renderer topology uniforms. Analysis and surface
lattices must now match instead of being silently tiled through modulo
indexing. Trail routing retains explicit chart-cut avoidance; that is a route
planning constraint, not a second topology.

## Stage: authoritative surface bundle (done)

`Surface::geometry()` is the one finite terrain store. Its mandatory
typed columns are elevation in metres, normal, removed material, deposited
material, and broad snow support. Elevation is an affine quantity point with
the native representation of a `float`; it is neither boxed nor normalized.
`GeneratedWorld` owns one `Surface`; there is no preceding height map and no
geometry refresh copy.

Generation, physics, hydrology, and rendering all read or mutate those
columns. Analysis entry points accept any
`TerrainDomain` bundle containing `surface_elevation`; there is no borrowed
terrain adapter. The renderer accepts the typed elevation and normal columns
directly. Compile-time layout checks guarantee their representations are the
native `float` and `Vec3` payloads expected by GPU upload.

Deleted with the old layer:

- `HeightMap`, `NormalComputingHeightMap`, `RandomHeightMap`, `NormalMap`, and
  `Array2D`;
- `moppe/map/generate.hh` and `generate.cc`;
- raw height/normal/ledger pointers;
- `Surface::refresh` and the copied geometry materialization barrier.

## Stage: physical elevation (done)

The authoritative geometry column, CPU reconstruction, hydrology, terrain
evolution, trails, water sheets, renderer textures, and shaders share one
physical elevation frame in metres. `SurfaceElevation` is an affine point;
water elevation uses the same specification, so subtraction yields physical
depth. GPU height and water lanes contain metre floats unchanged, with a
vertical shader scale of one.

The constructor's vertical world extent remains a camera/shadow bound. It is
not a calibration for stored elevations. Relative-elevation methods,
analysis-side height scales, and presentation normalization are deleted.

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

This pass removed 3,026 net lines. `TerrainDomain` is now the shared finite
domain for the surface and geological bundles.

## Stage: literal world construction (done)

The terrain had one legal program: geology, orogeny, then trails. That order
is now the source code in `world_loading.cc`, implemented by three direct free
operations in `map/terrain_generation.*`. `WorldRecipe` contains the physical
world identity plus the stream-power and trail algorithm values; it no longer
contains a vector of variant stages.

Deleted with the false programming model:

- `TerrainProgram`, `TerrainTransform`, and `OrogenyEvolution`;
- transform descriptions, property metadata, normalized editor controls, and
  their adapters;
- the stateful `TerrainEvaluator`, its checkpoints, replay ledger, and channel
  memory handoff;
- public nested geology-parameter structs whose only consumer was the editor.

Terrain Lab was the sole reason to retain a second mutable execution path
through generation and presentation. It and its launch/capture modes were
removed rather than preserved as a dormant framework. Stream-power evolution,
trail formation, water capture, benchmarks, and the ordinary game remain.

## Stage: one lattice value (done)

`TerrainGrid`, `TerrainDiscretization`, and `TerrainView` are deleted.
`TerrainDomain` owns periodic width, height, spacing, area, indexing, and
continuous reconstruction. Flood, drainage, merge-tree, trail, and waterline
results retain that same domain value. Inputs that need ground elevation are
constrained by the `TerrainElevations` bundle concept. Wet and fractional
drainage take only flood/census products because the filled routing surface,
not the original ground column, is their actual input.

`Surface` now exposes its mandatory geometry as one bundle instead of parallel
raw column getters. `sample_spacing()` and `world_extent()` name the remaining
3D presentation readings explicitly; neither participates in elevation
storage.

## Stage: typed analysis handoff (done)

Moisture analysis, waterline proximity, and trail analysis now return their
final quantity columns as finite bundles over `TerrainDomain`. The surface
adopts those bundles directly. It no longer accepts arbitrary float spans,
checks only their length, loops over them again, and invents the missing
semantics during a copy.

Trail and home-base influence are one `TrailUseMap`, because they are produced
and become valid together. The renderer bridge remains the deliberate place
where typed quantities become homogeneous float texture lanes.

## Stage: typed hydrology and consolidated surface readings (done)

The compatibility `ScalarRaster`, typed `Raster`, and `RasterDomain` are
deleted. Flood level and depth, classic drainage slope and contributing area,
and painted water sheets are named typed bundles over `TerrainDomain`.

`SurfaceDomain` and `SurfaceAtlas` are also deleted. `Surface` owns mandatory
geometry plus one derived `SurfaceReadings` bundle. The latter replaces seven
optional section bundles and their repeated domain copies. Generic
neighbourhood operations, including the Atelier Laplacian, remain available in
`spatial/bundle_operations.hh` without enlarging the core storage header.
