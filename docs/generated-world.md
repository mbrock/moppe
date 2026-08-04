# Generated worlds

`game::GeneratedWorld` is the non-copyable, non-movable, renderer-free owner
of one completed landscape. It is assembled from finished values; holding one
means no construction stage remains optional or in progress.

## Finished artifacts

| Artifact | Meaning |
| --- | --- |
| `WorldParams` and `WorldRecipe` | bound extent, resolution, datum, seed, profile, and algorithms |
| `map::SurfaceGeometry` | authoritative elevation, normals, material history, and snow support |
| `game::Hydrology` | flood, lakes, wet drainage, and rivers in derivation order |
| `terrain::WaterSheets` | water elevation, wave amplitude, and velocity |
| `terrain::TrailNetwork` | built route, alignment, grading report, and typed use readings |
| `map::SurfaceReadings` | completed hydrology, geology, ecology, and trail readings over the ground |

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
  -> construct GeneratedWorld from the finished parts
```

The loading screen sees `LoadingStatus`, not candidate terrain. The deleted
terrain preview and generation-history queue are not part of the current
handoff.

The Apple TV install path runs the complete renderer-free world pipeline once
in a native Release baker. It bundles typed Arrow fields for terrain, flood,
drainage, water, trail use, and surface readings, plus compact lake, river, and
trail topology for the default Play-profile, seed-123 world. The bundle also
carries the renderer-independent global forest plan, avoiding hundreds of
thousands of habitat samples during device startup. Its
television-specific 1024-square lattice retains the Play erosion age while
bounding asset size. A normal Apple TV launch constructs `GeneratedWorld`
directly from that bundle; only renderer resources, actors, and presentation
meshes initialize on device. The recipe, cache schema, forest seed, and world
extent are checked before loading, while alternate seeds and profiles retain
the ordinary generated,
writable-cache path.

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
