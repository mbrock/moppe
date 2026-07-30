# RFC-0001: Evolve the current engine toward the Atelier architecture

Status: realized (decision accepted)

## Decision

Moppe will not be replaced by a second engine. It will adopt the Atelier's
architecture organ by organ, preserving a working game at every stage.

The desired shape is:

```text
topological domain
  -> typed intrinsic sections
  -> focused laws and simulation
  -> explicit extrinsic presentation
```

At the time of the decision, the `Surface`/`WaterSurface` atlas and the
Atelier tree were proofs of this shape. Later harmonization dissolved those
wrappers into direct typed bundles without changing the decision. The
completed execution track is
[`current-engine-refactoring`](../tracks/current-engine-refactoring/README.md).
The reader-facing result is the [engine atlas](../../docs/engine-atlas.md).

## Constraints

- Prefer concrete domain values over generic manager frameworks.
- Preserve the current game and its deterministic checks while boundaries move.
- Keep quantities and their semantic kinds until the explicit renderer bridge.
- Treat generation, mutable simulation, and presentation as different kinds of
  state.
- Let the source and target graph follow the proven dependencies, rather than
  moving files first.

## Result

The RFC is realized. `spatial::Bundle` is the shared finite-section
abstraction; `GeneratedWorld` owns completed renderer-free world artifacts;
`GameSession` and its `GameState` isolate mutable runtime state; `FrameView`
and focused presenters read those values; and the CMake graph now names the
world, simulation, scene, application, renderer, and platform boundaries.
The closed track retains the individual decisions and validation evidence that
produced this shape.

## Deferred and intentionally not claimed

- No work item was dropped: all 19 items in the execution track are done.
- Registered frame projections described by the Atelier-earth proposal remain
  future work. Later work completed the seamless periodic lattice and physical
  elevation frame without reopening this RFC.
- Session checkpoints support fixed-world replay, not complete determinism of
  generation, loading, renderer history, or window state.
- Metal was the implemented backend at closure. WebGPU landed afterwards;
  Android remains unimplemented. Neither was an RFC-0001 deliverable.

## Completion record

The original completion criteria are met: the game has one shared
finite-section abstraction, an explicit generated-world value, an explicit
mutable game-session value, a scene presentation reading those values, and a
target graph that reflects that direction.
