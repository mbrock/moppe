# Generated worlds

`game::GeneratedWorld` is the stable, renderer-free owner of one completed
world. It binds the immutable physical description (`WorldRecipe` and its
`WorldParams`) to one authoritative typed surface and its
materialized readings. Gameplay does not own parallel copies of these
artifacts.

## What it owns

| Artifact | Valid after |
| --- | --- |
| `Surface` and its mandatory `SurfaceGeometry` | construction |
| Geometry normals and snow support | `rebuild_surface()` |
| `Hydrology` | `analyze_hydrology()` |
| `WaterSurface`, `SurfaceReadings`, and trail network | `derive_surface_readings()` |

`Hydrology` is one named, complete value: standing water, lake census, wet
drainage, fractional channel drainage, water-body flow, and river network are
constructed together. A normal completed world therefore cannot expose, for
example, rivers without the flood and drainage readings they depend on.
`WaterSurface` and the trail network use optional storage to express their
construction boundary and to support focused tests, but ordinary completed
worlds build both.

## Mutability and lifetime

`GeneratedWorld` is non-copyable and non-movable. `GameSession`'s vehicles
and glider borrow its one surface. `MoppeGame`
therefore keeps the active world behind an owning `unique_ptr`: a worker builds
a fresh completed world, and the main thread transfers that owner exactly once
at `activate_completed_world()`.

At activation, the outgoing session and world remain alive while the completed
owner becomes
active and a fresh session binds its new surface. The retired
session then releases its old borrows before the retired world is destroyed.
Only after that handoff does the main thread build renderer-facing river,
water, ground, forest, and actor presentation. The active world is
consequently never half-mutated by a loading worker.

`WorldLoading` owns generation as a deliberately single-flight operation:
running builds are never cancelled. Its job owns the requested recipe and
shares only the loader's internal state, never a raw pointer back to
`MoppeGame`. The platform retains that state through its main-thread
completion callback, so closing the application cannot leave the worker with
an application borrow. A failed build retains the existing clear failure
behavior: it logs the generation error and exits rather than exposing an
incomplete candidate.

While generation runs, `WorldLoading` shares only status text with the
loading screen; the screen neither borrows nor owns a candidate world's
terrain state. Once the platform callback marks the candidate complete,
`MoppeGame` polls and transfers the completed owner on the main thread
before beginning renderer-facing activation.

Ordinary gameplay receives const readings. The loading worker calls the
three build steps (`rebuild_surface`, `analyze_hydrology`,
`derive_surface_readings`) in order after evaluating the terrain, through the
mutable `surface()` overload.

## Checks

`tests/game/generated_world_test.cc` verifies the named completed artifacts,
their materialized geometry and readings, and that a non-movable world
transfers by its owner rather than a value move. The deterministic
water-capture tool exercises the runtime construction path:

```sh
tools/capture-water /tmp/river.png river
```
