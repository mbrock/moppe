# Generated worlds

`game::GeneratedWorld` is the stable, renderer-free owner of one completed
world. It binds the immutable physical description (`WorldRecipe` and its
normalized `WorldParams`) to the authoritative height field and its materialized
readings. Gameplay does not own parallel copies of any of these artifacts.

## What it owns

| Artifact | Valid after |
| --- | --- |
| `RandomHeightMap` and terrain history | terrain evaluation or cache load |
| `Surface` and its `SurfaceAtlas` geometry | `Builder::rebuild_surface()` |
| `Hydrology` | `Builder::analyze_hydrology()` |
| `WaterSurface`, ground hydrology, ecology, geology, and trail sections | `Builder::materialize_analyses()` |

`Hydrology` is one named, complete value: standing water, lake census, wet
drainage, fractional channel drainage, water-body flow, and river network are
constructed together. A normal completed world therefore cannot expose, for
example, rivers without the flood and drainage readings they depend on.
`WaterSurface` and the trail network remain optional because a Terrain Lab
preview may intentionally omit hydrology and the terrain program may omit a
trail stage.

## Mutability and lifetime

`GeneratedWorld` is non-copyable and non-movable. Vehicles, the glider, and
the Terrain Lab borrow its terrain or surface. `MoppeGame` therefore keeps the
active world behind an owning `unique_ptr`: a worker builds a fresh completed
world, and the main thread transfers that owner exactly once at
`activate_completed_world()`.

At activation, Terrain Lab first restores and releases its raw borrows. The
outgoing owner remains alive while the two vehicles and glider are destroyed;
the completed owner becomes active; then those three borrowers are constructed
again against the new terrain and surface. Only after that handoff does the
main thread build renderer-facing river, water, ground, forest, and actor
presentation. The active world is consequently never half-mutated by a
loading worker.

Generation is deliberately single-flight: running builds are never cancelled.
Its job owns the requested recipe until the platform's main-thread completion
callback has observed a published completed owner. The platform retains that
job through the callback; its lifetime mutex keeps worker and callback game
access behind the same shutdown boundary. `MoppeGame` takes that mutex before
it clears the job's raw game pointer, so closing during generation waits for
the worker and a queued callback simply does nothing. A failed build retains
the existing clear failure behavior: it logs the generation error and exits
rather than exposing an incomplete candidate. While it runs, the loading
screen sees only copied height snapshots in its separate preview map; it
neither borrows nor owns a candidate world's terrain state.

Ordinary gameplay receives const readings. `Builder` is the explicit capability
used by the loading worker to evaluate terrain, record history, rebuild the surface,
analyze hydrology, and materialize the derived atlas and water surface.
Terrain Lab has one deliberately named mutable borrow,
`terrain_for_terrain_lab()`: its model restores the source map when the Lab
leaves, so it does not make a permanently edited world implicit.

## Checks

`tests/game/generated_world_test.cc` verifies the named completed artifacts,
their materialized atlas groups, and that a non-movable world transfers by its
owner rather than a value move. The deterministic Terrain Lab and water-capture
tools exercise the two runtime consumers:

```sh
tools/capture-terrain-lab /tmp/terrain-lab.png
tools/capture-water /tmp/river.png river
```
