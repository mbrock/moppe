+++
id = "ENG-031"
title = "Collect mutable runtime state in GameSession"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "done"
depends_on = ["ENG-023", "ENG-030"]
order = 110
areas = ["simulation", "game"]
+++

# Collect mutable runtime state in GameSession

## Outcome

The player modes, actors, camera, effects, score, and session clock have one
concrete mutable owner attached to an immutable generated world.

## Scope

Retain distinct bike, car, foot, and glider semantics. Avoid an ECS or a
generic actor manager.

## Acceptance

- `MoppeGame` no longer privately owns the bulk of gameplay fields.
- A session has a clear value-state/checkpoint boundary.
- Immutable world references and physical configuration remain outside it.

## Evidence

`game::GameSession` is a concrete, non-copyable runtime owner of
`GameLogicState`, bike, car, glider, walker, chase camera, stars, and dust.
Its `State` is the existing copyable `GameState`; `state()` and `restore()`
gather the same payload without copying a generated world, renderer resource,
or physical configuration. `MoppeGame` now owns one session pointer rather
than those gameplay fields directly. At a completed-world handoff it keeps the
old session and world alive, binds a new session to the new world, then retires
the old session before the old world so vehicle and glider borrows release in
the correct order. Regeneration intentionally begins a fresh session.

`game_session_restores_a_same_world_checkpoint` checks both restoration within
one session and transfer into another session prepared against the same world.
`./tools/format --check`, `git diff --check`, `cmake --build build -j 2`,
`./build/moppe-tests` (194 tests), `ctest --test-dir build
--output-on-failure`, and `./tools/plan check` passed. Water, Terrain Lab, and
in-process regeneration captures wrote `/tmp/moppe-eng031-water.png`,
`/tmp/moppe-eng031-lab.png`, and `/tmp/moppe-eng031-regen.png`.
