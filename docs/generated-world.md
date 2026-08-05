# Generated worlds

`game::GeneratedWorld` is the non-copyable, non-movable, renderer-free owner
of one completed landscape. It is assembled from finished values; holding one
means no construction stage remains optional or in progress.

## Finished artifacts

| Artifact | Meaning |
| --- | --- |
| `WorldParams` and `WorldRecipe` | bound extent, resolution, datum, seed, profile, and algorithms |
| `map::SurfaceGeometry` | authoritative elevation, mobile sediment, geological material history, normals, and snow support |
| `game::Hydrology` | flood, lakes, wet drainage, and rivers in derivation order |
| `terrain::WaterSheets` | water elevation, wave amplitude, and velocity |
| `terrain::TrailNetwork` | built route, alignment, grading report, and typed use readings |
| `map::SurfaceReadings` | completed hydrology, geology, ecology, and trail readings over the ground |
| `game::ForestPlan` | stable renderer-free tree sites, forms, and identities across the world |

`Hydrology` is a tuple of distinct stage types. Code that consumes the complete
analysis binds the four values by name; focused application access uses their
types. A normal completed world cannot expose rivers without the flood, lake,
and drainage results from which they were derived.

## Construction

The mutable construction capability lives in `world_loading.cc`, not on
`GeneratedWorld`:

```text
allocate SurfaceGeometry
  -> load typed cache or initialize/evolve/form trails
  -> rebuild geometry readings
  -> analyze hydrology
  -> paint water and derive surface readings
  -> plan the global forest
  -> construct GeneratedWorld from the finished parts
```

The loading screen sees `LoadingStatus`, not candidate terrain. The deleted
terrain preview and generation-history queue are not part of the current
handoff.

Every writable host saves that completed renderer-free world as a directory of
typed Arrow fields, compact topology, and the renderer-independent forest
plan. The automatic directory name contains a stable default namespace and the
complete recipe identity. Ordinary executable rebuilds therefore reuse the
same finished world instead of rerunning erosion. Profile, resolution, seed,
extent, and water datum still select separate worlds, and the stored schema and
recipe are validated before reuse. An explicit `--uplift-years YEARS`
experiment becomes part of that immutable recipe and both cache identities,
so forcing-matrix launches cannot reuse another schedule's terrain.
`--world-cache-key NAME` selects an additional stable developer namespace.
`--refresh-world-cache` rebuilds and replaces the selected entry, while
`--no-world-cache` retains the older terrain-only cache path without loading or
saving a completed world.

The Apple TV install path additionally runs the same pipeline once in a native
Release baker and bundles the directory for the default Play-profile, seed-123
world. Its
television-specific 1024-square lattice retains the Play erosion age while
bounding asset size. A normal Apple TV launch constructs `GeneratedWorld`
directly from that bundle; only renderer resources, actors, and presentation
meshes initialize on device. The recipe, cache schema, forest seed, and world
extent are checked before loading, while alternate seeds and profiles retain
the ordinary writable-cache path.

`WorldLoading` owns a single-flight job containing the requested parameters
and recipe. The worker retains a shared loader state rather than a raw
`MoppeGame` pointer, reports progress under a narrow mutex, and publishes one
`unique_ptr<GeneratedWorld>`. Failure logs the exception and exits instead of
publishing a partial world.

## Activation and borrowing

The main thread takes a completed owner exactly once. When replacing an active
world, `MoppeGame`:

1. retains the outgoing world and session;
2. installs the completed world;
3. binds a fresh `GameSession` to its geometry;
4. destroys the retired session, releasing its borrows; then
5. destroys the retired world.

Only after activation does the main thread build renderer resources, forests,
fixed-size waterfall curtains, actor placement, and the opening journey. The loading worker
never mutates the active world or owns GPU resources.

Ordinary gameplay reads the completed artifacts through const accessors.
`GameState` can be restored only against a session prepared for the same
world; terrain and hydrology do not enter the checkpoint.

## Checks

Focused tests cover finished-artifact construction and ownership. The short
runtime acceptance path exercises generation, handoff, renderer activation,
and capture:

```sh
MOPPE_TERRAIN_PROFILE=smoke tools/capture-game /tmp/moppe-smoke.png
```

`MOPPE_REGENERATE_ONCE=1` exercises the same ownership transition twice.
Feature-targeted water captures use the full normal construction path while
selecting a deterministic hydrological subject.
