# RFC-0001: Evolve the current engine toward the Atelier architecture

Status: accepted

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

The current `Surface`/`WaterSurface` atlas and the Atelier tree are proofs of
this shape. The execution track is
[`current-engine-refactoring`](../tracks/current-engine-refactoring/README.md).

## Constraints

- Prefer concrete domain values over generic manager frameworks.
- Preserve the current game and its deterministic checks while boundaries move.
- Keep quantities and their semantic kinds until the explicit renderer bridge.
- Treat generation, mutable simulation, and presentation as different kinds of
  state.
- Let the source and target graph follow the proven dependencies, rather than
  moving files first.

## Completion criteria

This RFC is realized when the game has one shared finite-section abstraction,
an explicit generated-world value, an explicit mutable game-session value, a
scene presentation reading those values, and a target graph that reflects that
direction.
