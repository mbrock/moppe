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
the Terrain Lab borrow its terrain or surface, so regeneration resets the
existing subobjects in place rather than replacing the world. A reset accepts
only a recipe with the same terrain storage layout; it clears derived artifacts
and reseeds the existing map before construction starts again.

Ordinary gameplay receives const readings. `Builder` is the explicit capability
used by loading to evaluate terrain, record history, rebuild the surface,
analyze hydrology, and materialize the derived atlas and water surface.
Terrain Lab has one deliberately named mutable borrow,
`terrain_for_terrain_lab()`: its model restores the source map when the Lab
leaves, so it does not make a permanently edited world implicit.

The current loading task still builds that stable value directly while reporting
progress. The later asynchronous-handoff step can establish one handoff around
a completed `GeneratedWorld` without first having to discover which parallel
optional in `MoppeGame` belongs with it.

## Checks

`tests/game/generated_world_test.cc` verifies the named completed artifacts,
their materialized atlas groups, and in-place reset addresses without a
renderer. The deterministic Terrain Lab and water-capture tools exercise the
two runtime consumers:

```sh
tools/capture-terrain-lab /tmp/terrain-lab.png
tools/capture-water /tmp/river.png river
```
