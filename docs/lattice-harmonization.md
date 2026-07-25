# Lattice harmonization

The game is a couple dozen named fields on one lattice, plus a motorcycle.
Three generations of representation never finished replacing each other:
the 2008 `HeightMap` family (storage seam, raw floats, virtual `get`),
the portable-terrain layer (`TerrainView`/`TerrainGrid`/`ScalarRaster`,
unique lattice), and the typed bundles (`spatial::Bundle`, `SurfaceAtlas`).
Three grid types describe the same lattice.  This document is the plan for
finishing the port.

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
  `SurfaceDomain`, `HeightMap`).
- `TerrainGrid.unique_width/height/size` — become synonyms of
  width/height/size in this stage; a follow-up mechanical rename deletes
  them across the analyses.
- `map/generate` — `HeightMap`: scale = size/width, `periodic ()` and
  `in_bounds` clamping deleted, interpolation wraps the +1 neighbor;
  `synchronize_periodic_edges` and the ledger twin deleted;
  `compute_normal_map` keeps only the wrapped branch, minus seam writes;
  cache format unchanged in shape (dimensions now unique; build-id keying
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

Next mechanical follow-up: rename the unique_width/height/size synonyms
(22 files) to plain width/height/size and delete them.

## Later stages

1. **One domain type** — collapse `TerrainGrid`, `SurfaceDomain`,
   `FieldSamplingGrid2D` into a single lattice description.
2. **Elevation joins the bundle** — retire `HeightMap`/`NormalMap`/
   `Array2D`; elevation and normals are typed fields; physics samples
   them like every other field.  The 2008 layer's obituary.
